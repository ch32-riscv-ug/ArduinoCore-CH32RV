"""i2c_loopback's conversation, replayed by smoke.run_directory().

Every check reports PASS, or SKIP on a single-bus series. Missing pull-ups fail
`slave_acks_address` with code 5 (bus timeout); a missing jumper fails it with 2
(address NACK). The code is on the FAIL line.
"""


def expect(console) -> None:
    console.expect_exact("i2c_loopback READY", timeout=20)
    console.write("RUN\n")
    console.expect(r"slave_acks_address (PASS|SKIP .*)")
    console.expect(r"other_address_nacked (PASS|SKIP .*)")
    console.expect(r"write_delivered (PASS|SKIP .*)")
    console.expect(r"receive_event_once (PASS|SKIP .*)")
    console.expect(r"receive_count (PASS|SKIP .*)")
    console.expect(r"receive_bytes (PASS|SKIP .*)")
    console.expect(r"request_reply (PASS|SKIP .*)")
    console.expect(r"overread_gets_ff (PASS|SKIP .*)")
    console.expect(r"full_buffer_delivered (PASS|SKIP .*)")
    console.expect(r"second_round_works (PASS|SKIP .*)")
    console.expect_exact("i2c_loopback done failures=0")
