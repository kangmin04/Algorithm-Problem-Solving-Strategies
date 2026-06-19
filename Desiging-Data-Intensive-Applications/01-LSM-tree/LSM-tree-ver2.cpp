/* Compaction 및 bloomFilter 추가됨. */

#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <bitset>

using namespace std;

// 1. 단순화한 블룸 필터 (Bloom Filter) 구조체
struct BloomFilter {
    bitset<1024> bits; // 1024비트 공간 생성

    // 간단한 해시 함수 2개 구현 (C 언어풍 정수 해싱)
    size_t hash1(const string& key) const { 
        size_t h = 5381;
        for (char c : key) h = ((h << 5) + h) + c;
        return h % 1024;
    }
    size_t hash2(const string& key) const {
        size_t h = 0;
        for (char c : key) h = (h * 31) + c;
        return h % 1024;
    }

    // Key 등록
    void insert(const string& key) {
        bits.set(hash1(key));
        bits.set(hash2(key));
    }

    // Key 존재 여부 확인 (False일 확률은 100% 신뢰)
    bool maybeExists(const string& key) const {
        return bits.test(hash1(key)) && bits.test(hash2(key));
    }
};

// 2. 블룸 필터가 탑재된 확장형 SSTable 구조체
struct SSTable {
    string filename;
    string minKey;
    string maxKey;
    BloomFilter filter; // 파일마다 블룸 필터를 동봉합니다.
};

class CompleteLSMTree {
private:
    map<string, string> memTable;
    size_t maxMemTableSize;
    vector<SSTable> diskSSTables;
    int fileCounter;

    void flushToSSTable() {
        if (memTable.empty()) return;

        string filename = "sstable_" + to_string(++fileCounter) + ".db";
        ofstream outFile(filename);

        SSTable sst;
        sst.filename = filename;
        sst.minKey = memTable.begin()->first;
        sst.maxKey = memTable.rbegin()->first;

        for (const auto& pair : memTable) {
            outFile << pair.first << "," << pair.second << "\n";
            sst.filter.insert(pair.first); // SSTable을 쓸 때 블룸 필터에도 기록!
        }
        outFile.close();
        memTable.clear();

        diskSSTables.push_back(sst);
        cout << "[Flush] 디스크에 파일 생성: " << filename << "\n";

        // 트리거: 파일이 3개 이상 쌓이면 컴팩션(병합)을 시작합니다.
        if (diskSSTables.size() >= 3) {
            triggerCompaction();
        }
    }

    //  3. 컴팩션(Compaction) - 여러 개의 SSTable을 하나로 병합
    void triggerCompaction() {
        cout << "\n⚡ [Compaction 시작] 파편화된 디스크 파일들을 병합하고 최신화합니다...\n";

        // 모든 파일의 데이터를 모아서 최신 데이터로 덮어쓸 임시 맵 생성
        // (실제 대용량 DB는 두 파일의 포인터를 비교하는 Merge Sort를 하지만, 여기서는 개념 이해를 위해 Map 사용)
        map<string, string> mergedData;

        // 과거 파일부터 순서대로 읽어서 mergedData에 부어버립니다. 
        // 자연스럽게 같은 Key가 있다면 최신 파일의 Value가 과거의 Value를 덮어씁니다!
        for (const auto& sst : diskSSTables) {
            ifstream inFile(sst.filename);
            string line;
            while (getline(inFile, line)) {
                size_t delim = line.find(',');
                string k = line.substr(0, delim);
                string v = line.substr(delim + 1);
                mergedData[k] = v; // 중복 제거 및 최신 데이터로 갱신
            }
            inFile.close();
            
            // 병합 완료된 물리적 파일 삭제 (가상으로 로그만 출력)
            cout << " - 기존 구형 파일 제거 완료: " << sst.filename << "\n";
        }

        // 기존 SSTable 관리 목록 초기화
        diskSSTables.clear();

        // 병합된 단 하나의 깨끗한 대형 SSTable 파일을 새로 생성
        string compactFilename = "sstable_compacted_" + to_string(++fileCounter) + ".db";
        ofstream outFile(compactFilename);

        SSTable newSst;
        newSst.filename = compactFilename;
        newSst.minKey = mergedData.begin()->first;
        newSst.maxKey = mergedData.rbegin()->first;

        for (const auto& pair : mergedData) {
            outFile << pair.first << "," << pair.second << "\n";
            newSst.filter.insert(pair.first); // 새 블룸필터 갱신
        }
        outFile.close();

        diskSSTables.push_back(newSst);
        cout << "🎉 [Compaction 완료] 새 통합 파일 생성: " << compactFilename << "\n\n";
    }

public:
    CompleteLSMTree(size_t maxSize) : maxMemTableSize(maxSize), fileCounter(0) {}

    void put(const string& key, const string& value) {
        memTable[key] = value;
        if (memTable.size() >= maxMemTableSize) {
            flushToSSTable();
        }
    }

    string get(const string& key) {
        // 1. 메모리 먼저 확인
        if (memTable.find(key) != memTable.end()) {
            return memTable[key] + " (Found in MemTable)";
        }

        // 2. 디스크 파일 검색 (최신 파일 목록부터 역순으로)
        for (int i = diskSSTables.size() - 1; i >= 0; --i) {
            const auto& sst = diskSSTables[i];

            // 💡 [치트키 1] 블룸 필터 검사: 확실히 없다고 하면 파일 열지도 않고 통과!
            if (!sst.filter.maybeExists(key)) {
                cout << "🔍 [Bloom Filter Check] " << sst.filename << " 에는 확실히 없으므로 패스!\n";
                continue;
            }

            // 💡 [치트키 2] 최소/최대 키 범위 검사
            if (key >= sst.minKey && key <= sst.maxKey) {
                ifstream inFile(sst.filename);
                string line;
                while (getline(inFile, line)) {
                    size_t delim = line.find(',');
                    string k = line.substr(0, delim);
                    string v = line.substr(delim + 1);
                    if (k == key) {
                        return v + " (Found in " + sst.filename + ")";
                    }
                }
            }
        }
        return "Not Found";
    }
};

int main() {
    CompleteLSMTree lsm(2); // 메모리 버퍼 크기 2

    // 파일 1 생성 유도
    lsm.put("user1", "Alice_V1");
    lsm.put("user2", "Bob");

    // 파일 2 생성 유도 (user1 수정본 진입)
    lsm.put("user3", "Charlie");
    lsm.put("user1", "Alice_V2"); // 같은 Key의 수정 데이터

    // 파일 3 생성 유도 ➡️ 파일이 3개가 되는 순간 컴팩션 발동 조건 충족!
    lsm.put("user4", "David");
    lsm.put("user5", "Elena");

    cout << "--- 데이터 검색 테스트 ---\n";
    // user1을 조회하면 과거 데이터는 날아가고 컴팩션으로 합쳐진 단 하나의 파일에서 찾아옵니다.
    cout << "결과: " << lsm.get("user1") << "\n\n";

    // 존재하지 않는 가상의 데이터를 조회할 때 블룸 필터가 어떻게 작동하는지 확인
    cout << "결과: " << lsm.get("ghost_user") << "\n";

    return 0;
}