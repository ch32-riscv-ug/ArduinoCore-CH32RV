#!/usr/bin/env python3
"""Preserve locked N03 slave sources and locate evidence; no silicon validation."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    lock = root / 'planning/core-redesign/i2c-inputs.json'
    sources = [row for row in json.loads(lock.read_text())['sources']
               if row['file'] in ('CH32V003RM.PDF', 'CH32V003-i2c.c')]
    assert len(sources) == 2
    args.out.mkdir(parents=True, exist_ok=False)
    records = []
    for row in sources:
        source = args.source_dir / row['file']
        assert source.stat().st_size == row['bytes']
        assert digest(source) == row['sha256'], source
        shutil.copy2(source, args.out / row['file'])
        records.append(dict(row, verified=True))
    shutil.copy2(lock, args.out / 'i2c-inputs.json')
    shutil.copy2(Path(__file__), args.out / Path(__file__).name)
    command = ['pdftotext', '-layout', str(args.out / 'CH32V003RM.PDF'),
               str(args.out / 'CH32V003RM.txt')]
    subprocess.run(command, check=True, capture_output=True)
    text = (args.out / 'CH32V003RM.txt').read_text()
    pages = text.split('\f')
    # Locations only. The report does not infer hardware correctness from text hits.
    sections = ['13.4 Slave Mode', '13.5 Error Conditions', '13.6 Clock Extension',
                '13.11.6 I2C Status Register 1', '13.11.7 I2C Status Register 2']
    locations = {}
    for section in sections:
        matching = [i + 1 for i, page in enumerate(pages) if section in page]
        assert matching, section
        locations[section] = matching
    first = text.index('13.4 Slave Mode', text.index('13.4 Slave Mode') + 1) if text.count('13.4 Slave Mode') > 1 else text.index('13.4 Slave Mode')
    end = text.index('13.7 SMBus', first)
    (args.out / 'slave-sections.txt').write_text(text[first:end])
    sdk = (args.out / 'CH32V003-i2c.c').read_text()
    routines = {}
    for name in ('I2C_CheckEvent', 'I2C_GetLastEvent'):
        signature = ('ErrorStatus ' if name == 'I2C_CheckEvent' else 'uint32_t ') + name + '('
        start = sdk.index(signature)
        finish = sdk.index('\n}', start) + 2
        body = sdk[start:finish]
        assert body.index('I2Cx->STAR1') < body.index('I2Cx->STAR2')
        routines[name] = dict(first_line=sdk[:start].count('\n') + 1,
                              reads_star1_then_star2=True)
    report = dict(scope='Locked source identity and location audit only; no register simulation or hardware execution',
                  sources=records, lock_sha256=digest(lock),
                  script_sha256=digest(Path(__file__)), command=command,
                  pdftotext_version=subprocess.run(['pdftotext', '-v'], capture_output=True, text=True).stderr.strip(),
                  text_sha256=digest(args.out / 'CH32V003RM.txt'),
                  section_pdf_pages_1_based=locations, sdk_routines=routines)
    (args.out / 'audit.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(sources=len(records), sections=locations, routines=routines)))


if __name__ == '__main__':
    main()
