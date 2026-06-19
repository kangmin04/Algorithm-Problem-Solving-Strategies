#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>

using namespace std;

// 디스크에 저장될 가상의 SSTable 구조
struct SSTable {
    string filename;
    string minKey;
    string maxKey;
};

class SimpleLSMTree {
private:
    map<string, string> memTable; // 메모리 버퍼 (자동 정렬되는 Red-Black Tree). memTable 임계값 차면 sstable파일로 디스크에 기록.  
    size_t maxMemTableSize;       // MemTable이 허용하는 최대 Key-Value 개수 
    vector<SSTable> diskSSTables; // 디스크에 저장된 SSTable 파일 목록
    int fileCounter;

    // MemTable의 내용을 디스크에 순차 쓰기(Flush)하는 함수
    void flushToSSTable() {
        if (memTable.empty()) return;

        string filename = "sstable_" + to_string(++fileCounter) + ".db";
        ofstream outFile(filename);

        string minKey = memTable.begin()->first;
        string maxKey = memTable.rbegin()->first;

        // 순차 쓰기 (Sequential Write) - 오버헤드가 매우 적음
        for (const auto& pair : memTable) {
            outFile << pair.first << "," << pair.second << "\n";
        }
        outFile.close();

        // 관리 목록에 추가
        diskSSTables.push_back({filename, minKey, maxKey});
        
        // 메모리 비우기
        memTable.clear();
        cout << "[Flush] 메모리가 가득 차 " << filename << "을 디스크에 순차 기록했습니다.\n";
    }

public:
    /* constructor로 maxMemTableSize, fileCounter정의 */
    SimpleLSMTree(size_t maxSize) : maxMemTableSize(maxSize), fileCounter(0) {}

    // 1. 쓰기 (Write): 무조건 메모리에만 냅다 꽂습니다.
    void put(const string& key, const string& value) {
        memTable[key] = value;

        // 임계치를 넘으면 디스크로 던짐
        if (memTable.size() >= maxMemTableSize) {
            flushToSSTable();
        }
    }

    // 2. 읽기 (Read): 메모리 먼저 보고, 없으면 디스크 파일들을 뒤집니다.
    string get(const string& key) {
        // 1단계: 메모리(MemTable) 검색 - $O(\log N)$
        if (memTable.find(key) != memTable.end()) { /* std::map::find는 실패시 map.end()를 반환함! */
            return memTable[key] + " (Found in MemTable)";
        }

        // 2단계: 디스크(SSTable) 검색 - 최신 파일부터 역순으로 탐색
        // 실제로는 블룸 필터로 파일 내 존재 여부를 먼저 체크하지만, 여기선 최소/최대 키 범위로 1차 필터링
        for (int i = diskSSTables.size() - 1; i >= 0; --i) {
            const auto& sst = diskSSTables[i];
            
            // 범위를 벗어나면 이 파일은 읽지도 않고 패스 (B-Tree의 범위 타기와 유사)
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
    // 메모리에 최대 2개만 들고 있는 LSM-Tree 생성
    SimpleLSMTree lsm(2);

    // 쓰기는 대단히 빠름 (메모리 작업 위주이기 때문)
    lsm.put("user1", "Alice");
    lsm.put("user2", "Bob");       // 데이터가 2개가 되는 순간 자동으로 플러시 발생
    lsm.put("user3", "Charlie");
    lsm.put("user1", "Updated_Alice"); // user1의 값을 수정 (메모리에 새로 추가됨)

    cout << "\n--- 데이터 조회 결과 ---\n";
    cout << "user3 조회: " << lsm.get("user3") << "\n";
    cout << "user1 조회: " << lsm.get("user1") << "\n"; // 가장 최근에 바뀐 데이터가 조회됨

    return 0;
}