// en: CH32H417 with CPU Core = V5F: the two cores talk. The V5F sends numbers to the V3F (CH32RV.ipcSend()), the V3F
// en: tests them for primes in loop1() and keeps totals under a hardware semaphore (CH32RV.hsemTake()); it prints
// en: each answer on its own UART, Serial1.
// ja: CH32H417 の CPU Core = V5F で、2 つのコアがやり取りする。V5F が V3F に数を送り（CH32RV.ipcSend()）、V3F が
// ja: loop1() で素数か調べて、ハードウェアのセマフォ（CH32RV.hsemTake()）の中で合計を数える。答えは V3F の
// ja: UART（Serial1）にも出す。
/* board: CH32H417 */

#include <CH32RV.h>

// en: HSEM channel 0 guards `totals`: two words that must be read together.
// ja: HSEM のチャネル 0 で `totals` を守る。2 つの語をまとめて読むため。
const uint8_t TOTALS_LOCK = 0;
struct Totals {
    uint32_t tested;
    uint32_t primes;
};
Totals totals;

// en: Set in each core's IPC interrupt, read in its loop.
// ja: それぞれのコアの IPC の割り込みで書き、ループで読む。
volatile uint32_t request = 0;   // the V3F's: a number to test, 0 = none
volatile uint32_t reply = 0;     // the V5F's: the last answer
volatile bool replied = false;

static bool isPrime(uint32_t n)
{
    if (n < 2) return false;
    for (uint32_t d = 2; d * d <= n; d++) {
        if (n % d == 0) return false;
    }
    return true;
}

// en: Runs on the V3F, in its IPC interrupt: keep the number for loop1().
// ja: V3F の IPC の割り込みで動く。数を loop1() に渡すだけ。
void onRequest(uint32_t n)
{
    request = n;
}

// en: Runs on the V5F, in its IPC interrupt.
// ja: V5F の IPC の割り込みで動く。
void onReply(uint32_t answer)
{
    reply = answer;
    replied = true;
}

// en: The V3F. ipcOnReceive() set here makes the IPC interrupt the V3F's; Serial1 begun here is the V3F's too.
// ja: V3F。ここで ipcOnReceive() を呼ぶと IPC の割り込みは V3F のものになる。ここで始めた Serial1 も V3F のもの。
void setup1()
{
    CH32RV.ipcOnReceive(onRequest);
    Serial1.begin(115200);
}

void loop1()
{
    const uint32_t n = request;
    if (n == 0) {
        return;
    }
    request = 0;
    const bool prime = isPrime(n);

    CH32RV.hsemTake(TOTALS_LOCK);
    totals.tested++;
    if (prime) totals.primes++;
    CH32RV.hsemRelease(TOTALS_LOCK);

    // en: The answer: the number with bit 31 set when it is prime. Waits up to 100 ms for the V5F to take the last.
    // ja: 答えは数に、素数なら bit 31 を立てたもの。前の答えが取られるまで最大 100 ms 待つ。
    if (!CH32RV.ipcSend(n | (prime ? 0x80000000u : 0u), 100)) {
        Serial1.println("V3F: the V5F did not take the last answer");
    }
    Serial1.print("V3F: ");
    Serial1.print(n);
    Serial1.println(prime ? " is prime" : " is not prime");
}

void setup()
{
    Serial.begin(115200);
    CH32RV.ipcOnReceive(onReply);
}

void loop()
{
    static uint32_t next = 1000003;
    static uint32_t sentAt = 0;

    if (CH32RV.ipcSend(next, 0)) {
        sentAt = millis();
        next += 2;
    }

    // en: Wait for the answer (up to 1 s).
    // ja: 答えを待つ（最大 1 秒）。
    while (!replied && millis() - sentAt < 1000) {
        delay(1);
    }
    if (!replied) {
        Serial.println("V5F: no answer from the V3F");
    } else {
        replied = false;
        const uint32_t answer = reply;

        CH32RV.hsemTake(TOTALS_LOCK);
        const Totals t = totals;
        CH32RV.hsemRelease(TOTALS_LOCK);

        Serial.printf("V5F: %lu %s (%lu of %lu tested are prime)\r\n", (unsigned long)(answer & 0x7FFFFFFFu),
                      (answer & 0x80000000u) ? "is prime" : "is not prime", (unsigned long)t.primes,
                      (unsigned long)t.tested);
    }
    delay(500);
}
