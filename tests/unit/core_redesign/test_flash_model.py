"""Byte-level media sensitivity checks; cut masks are declared, not silicon claims."""
import importlib.util
from pathlib import Path
import random

import pytest

spec = importlib.util.spec_from_file_location("exp04_medium", Path(__file__).with_name("flash_model.py"))
media = importlib.util.module_from_spec(spec); spec.loader.exec_module(media)
GEOMETRIES = [(2, 1024), (64, 64), (64, 1024), (256, 256), (256, 1024)]


@pytest.mark.parametrize("program,erase", GEOMETRIES)
@pytest.mark.parametrize("units", [1, 2, 4])
def test_read_and_one_time_program_then_erase(program, erase, units):
    m = media.Medium(program, erase, units)
    payload = bytes((3 + i * 13) & 255 for i in range(program))
    m.program(0, payload)
    assert m.read(0, program) == payload
    assert m.read(1, 1) == payload[1:2]  # Read need not share program alignment.
    assert m.read(len(m.data), 0) == b""
    before = bytes(m.data)
    with pytest.raises(ValueError):m.program(0, bytes(program))
    assert bytes(m.data) == before
    m.erase(0); assert m.read(0, erase) == bytes([255]) * erase
    m.program(0, payload); assert m.read(0, program) == payload


@pytest.mark.parametrize("program,erase", GEOMETRIES)
def test_all_ff_write_is_an_attempt_not_a_fresh_unit(program, erase):
    m = media.Medium(program, erase, 1); m.program(0, bytes([255]) * program)
    assert m.read(0, program) == bytes([255]) * program
    with pytest.raises(ValueError):m.program(0, bytes(program))
    m.erase(0); m.program(0, bytes(program))


@pytest.mark.parametrize("program,erase", GEOMETRIES)
@pytest.mark.parametrize("bits", [0x00, 0x01, 0x07, 0x55, 0xaa, 0xff])
def test_program_cut_changes_only_selected_bits_and_never_neighbors(program, erase, bits):
    m = media.Medium(program, erase, 3)
    m.program(0, bytes([0x12]) * program); m.program(2 * erase, bytes([0x34]) * program)
    before = bytes(m.data)
    with pytest.raises(media.PowerCut):m.program(erase, bytes(program), bytes([bits]) * program)
    assert m.read(erase, program) == bytes([255 - bits]) * program
    assert m.read(0, erase) == before[:erase] and m.read(erase + program, 2 * erase - program) == before[erase + program:]
    with pytest.raises(ValueError):m.program(erase, bytes(program))
    assert m.operations[-1] == ("program", erase, program, True)
    m.erase(erase);m.program(erase, bytes(program))


@pytest.mark.parametrize("program,erase", GEOMETRIES)
@pytest.mark.parametrize("bits", [0x00, 0x01, 0x07, 0x55, 0xaa, 0xff])
def test_erase_cut_does_not_infer_freshness_from_ff_bytes(program, erase, bits):
    m = media.Medium(program, erase, 3)
    for offset in range(0, len(m.data), program):m.program(offset, bytes(program))
    with pytest.raises(media.PowerCut):m.erase(erase, bytes([bits]) * erase)
    assert m.read(erase, erase) == bytes([bits]) * erase
    assert m.read(0, erase) == bytes(erase) and m.read(2 * erase, erase) == bytes(erase)
    for offset in range(erase, 2 * erase, program):
        with pytest.raises(ValueError):m.program(offset, bytes(program))
    m.erase(erase);m.program(erase, bytes(program))


@pytest.mark.parametrize("program,erase", GEOMETRIES)
def test_prefix_cut_and_invalid_commands_have_no_extra_effects(program, erase):
    m = media.Medium(program, erase, 2)
    with pytest.raises(media.PowerCut):m.program(0, bytes(program), bytes([255]) * (program // 2) + bytes(program // 2))
    assert m.read(0, program) == bytes(program // 2) + bytes([255]) * (program // 2)
    commands = [lambda:m.read(-1, 1), lambda:m.read(len(m.data), 1), lambda:m.read(0, -1),
                lambda:m.program(1, bytes(program)), lambda:m.program(erase, bytes(program - 1)),
                lambda:m.program(erase, bytearray(program)), lambda:m.program(erase, bytes(program), b""),
                lambda:m.erase(1), lambda:m.erase(0, bytes(erase - 1)), lambda:m.erase(len(m.data))]
    for command in commands:
        before = (bytes(m.data), list(m.attempted), list(m.operations))
        with pytest.raises(ValueError):command()
        assert before == (bytes(m.data), m.attempted, m.operations)


@pytest.mark.parametrize("program,erase", GEOMETRIES)
def test_generated_completed_operations_preserve_independent_expected_image(program, erase):
    rng = random.Random(0x4a120); m = media.Medium(program, erase, 4)
    expected = bytearray([255]) * (4 * erase); fresh = set(range(0, 4 * erase, program))
    for _ in range(200):
        if not fresh or rng.randrange(4) == 0:
            offset = rng.randrange(4) * erase;m.erase(offset)
            expected[offset:offset + erase] = bytes([255]) * erase
            fresh.update(range(offset, offset + erase, program))
        else:
            offset = rng.choice(sorted(fresh)); payload = bytes(rng.randrange(256) for _ in range(program))
            m.program(offset, payload); expected[offset:offset + program] = payload; fresh.remove(offset)
        assert m.read(0, len(expected)) == bytes(expected)


@pytest.mark.parametrize("geometry", [(0, 64, 1), (64, 63, 1), (2, 1024, 0), (True, 64, 1)])
def test_invalid_geometry_is_rejected(geometry):
    with pytest.raises(ValueError):media.Medium(*geometry)
