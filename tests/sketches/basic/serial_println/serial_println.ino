// Milestone 1 acceptance sketch: Serial must come up and print on every target board.
// Keep this sketch free of anything beyond Serial - it is the gate for "Serial works",
// not a peripheral test.

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("hello from ch32");
  Serial.print("int=");
  Serial.println(42);
  Serial.print("hex=");
  Serial.println(0xBEEF, HEX);
}

void loop()
{
}
