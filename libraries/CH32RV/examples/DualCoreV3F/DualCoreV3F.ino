// en: CH32H417 with CPU Core = V5F (the default): the V3F as a second core. setup1() / loop1() run there
// en: (started before setup()), and CH32RV.startV3F() hands it one function at a time when they are not used.
// ja: CH32H417 の CPU Core = V5F(既定)で、V3F を 2 つ目のコアとして使う。setup1() / loop1() は V3F で動く
// ja: (setup() の前に始まる)。使わないときは CH32RV.startV3F() で関数を 1 つずつ渡せる。
// en: On the V3F: interrupts and a peripheral the V5F does not use (Serial1 ...); not Serial, not the heap.
// ja: V3F では割り込みと、V5F が使わないペリフェラル（Serial1 など）が使える。Serial とヒープは使わない。
/* board: CH32H417 */

#include <CH32RV.h>

// en: Shared between the cores: volatile, one writer each.
// ja: 2 つのコアで共有する変数: volatile にし、書くのはどちらか片方だけにする。
volatile uint32_t primesFound = 0;
volatile uint32_t lastPrime = 0;

static bool isPrime(uint32_t n)
{
    if (n < 2) return false;
    for (uint32_t d = 2; d * d <= n; d++) {
        if (n % d == 0) return false;
    }
    return true;
}

// en: The V3F: count primes for ever, in the background.
// ja: V3F: 裏で素数を数え続ける。
void setup1()
{
}

void loop1()
{
    static uint32_t n = 2;
    if (isPrime(n)) {
        primesFound = primesFound + 1;
        lastPrime = n;
    }
    n++;
}

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    // en: The V5F reports what the V3F found, once a second.
    // ja: V5F が、V3F の見つけた数を 1 秒ごとに知らせる。
    Serial.print("V3F found ");
    Serial.print(primesFound);
    Serial.print(" primes, the last ");
    Serial.println(lastPrime);
    delay(1000);
}
