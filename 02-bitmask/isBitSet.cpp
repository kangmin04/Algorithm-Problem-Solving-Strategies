#include <iostream>
using namespace std;

bool isBitSet(unsigned long long a, int b) {
    // 1 뒤에 ULL을 붙여서 64비트 상자에서 비트를 밀도록 수정!
    return (a & (1ULL << b)) > 0; 
}

int main(){
    cout << "test does bit is in there" << endl;
    bool isBit = isBitSet(63, 4); 
    cout << "result isBit: " << isBit << endl;
    return 0;
}