// en: Play a short scale on a passive buzzer with tone().
// ja: tone()で受動ブザーに短い音階を鳴らす。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

static const uint8_t BUZZER = PA1;

static const unsigned int notes[] = {262, 294, 330, 349, 392, 440, 494, 523};
static const unsigned long beat = 200;

void setup()
{
    for (unsigned i = 0; i < sizeof notes / sizeof notes[0]; i++) {
        // en: Select one frequency in Hz and its duration in ms; the timer generates the pin changes.
        // ja: 周波数をHz、長さをmsで指定し、端子の切り替えはタイマーに任せる。
        tone(BUZZER, notes[i], beat);

        // en: tone() returns before playback ends; this delay schedules the next note.
        // ja: tone()は再生完了前に戻るので、次の音までの間隔をここで取る。
        delay(beat + 50);
    }
    noTone(BUZZER);
}

void loop()
{
}
