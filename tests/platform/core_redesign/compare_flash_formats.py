#!/usr/bin/env python3
"""EXP-04 conditional host format capacity/cut traces; no MCU or hardware upload."""
import argparse
from collections import Counter
import copy
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import zlib

REPO=Path(__file__).resolve().parents[3]
UNIT=REPO/"tests/unit/core_redesign"
INPUT=REPO/"planning/core-redesign/flash-format-inputs.json"
GEOMETRY=REPO/"planning/core-redesign/flash-inputs.json"


def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result)
    return result


media=module("exp04_media",UNIT/"flash_model.py")
formats=module("exp04_formats",UNIT/"flash_formats.py")


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def value(kind,generation):
    if kind=="keys":return {7:generation*119,11:generation*991}
    return {1:b"first-v1",2:b"old--v2!",3:b"next-v3!",4:b"last-v4!"}[generation]


def complete(store):
    while store.active is not None:store.step()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False);out=args.out.resolve()
    config=json.loads(INPUT.read_text());geometry=json.loads(GEOMETRY.read_text())
    report=dict(status="running",scope=__doc__,inputs={},rows=[],scenario_count=0,
                python=sys.version,python_binary_sha256=sha(sys.executable),zlib_version=zlib.ZLIB_RUNTIME_VERSION,
                mcu_code_bytes=None,mcu_ram_bytes=None,mcu_stall_time=None)
    for path in [Path(__file__).resolve(),INPUT,GEOMETRY,UNIT/"flash_formats.py",UNIT/"flash_model.py"]:
        relative=path.relative_to(REPO);dest=out/"inputs"/relative;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(path,dest);report["inputs"][str(relative)]=sha(path)
    def save():(out/"result.json").write_text(json.dumps(report,indent=2)+"\n")
    save()
    try:
        with (out/"cut-traces.jsonl").open("w") as traces:
            for target in geometry["targets"]:
                for mode,g in target["modes"].items():
                    for units in config["erase_units"]:
                        for candidate in config["candidates"]:
                            def make(m=None):
                                medium=m if m is not None else media.Medium(g["program_unit"],g["erase_unit"],units)
                                return formats.Store(medium,kind=candidate["kind"],policy=candidate["policy"],commit=candidate["commit"],blob_size=config["blob_bytes"],max_keys=config["max_numeric_keys"])
                            seed=make();row=dict(part=target["part"],mode=mode,erase_units=units,candidate=candidate["id"],program_unit=g["program_unit"],erase_unit=g["erase_unit"],**seed.layout())
                            if not row["supported"]:
                                try:seed.begin(value(seed.kind,1))
                                except formats.Refused:pass
                                else:raise AssertionError("unsupported candidate accepted")
                                assert not seed.medium.operations
                                row.update(normal_erase_operations=0,normal_program_operations=0,cut_scenarios=0,old_outcomes=0,new_outcomes=0,invalid_outcomes=0,unvalidated_other_outcomes=0)
                                report["rows"].append(row);save();continue
                            for generation in [1,2]:seed.begin(value(seed.kind,generation));complete(seed)
                            seed.begin(value(seed.kind,3));operations=list(seed.operations)
                            row["normal_erase_operations"]=sum(op[0]=="erase" for op in operations)
                            row["normal_program_operations"]=sum(op[0]=="program" for op in operations)
                            previous=value(seed.kind,2);new=value(seed.kind,3);outcomes=Counter()
                            for completed in range(len(operations)+1):
                                variations=["boundary"] if completed==len(operations) else ["boundary",*config["faults"]["masks"],"prefix"]
                                for variation in variations:
                                    medium=copy.deepcopy(seed.medium);writer=make(medium);writer.begin(new)
                                    for _ in range(completed):writer.step()
                                    if variation=="boundary":writer.abandon()
                                    else:
                                        op=writer.operations[writer.position];length=medium.erase_unit if op[0]=="erase" else medium.program_unit
                                        mask=bytes([255])*(length//2)+bytes(length-length//2) if variation=="prefix" else bytes([variation])*length
                                        try:writer.step(mask)
                                        except media.PowerCut:pass
                                        else:raise AssertionError("cut did not end operation")
                                    recovered=make(medium);got=recovered.load()
                                    outcome="old" if got["value"]==previous else ("new" if got["value"]==new else ("unvalidated-other" if got["status"]=="unvalidated" else "invalid"))
                                    if seed.policy=="two-bank":assert got["status"]=="valid" and outcome in ["old","new"]
                                    elif seed.kind!="raw" and got["status"]=="valid":assert outcome in ["old","new"]
                                    if completed==len(operations):assert outcome=="new"
                                    recover_corrupt=got["status"]=="corrupt"
                                    recovered.begin(value(seed.kind,4),recover_corrupt=recover_corrupt);complete(recovered)
                                    assert recovered.load()["value"]==value(seed.kind,4)
                                    trace=dict(part=target["part"],mode=mode,erase_units=units,candidate=candidate["id"],completed_operations=completed,cut=variation,status=got["status"],generation=got["generation"],outcome=outcome,reupdate=True)
                                    traces.write(json.dumps(trace)+"\n");outcomes[outcome]+=1;report["scenario_count"]+=1
                            row.update(cut_scenarios=sum(outcomes.values()),old_outcomes=outcomes["old"],new_outcomes=outcomes["new"],invalid_outcomes=outcomes["invalid"],unvalidated_other_outcomes=outcomes["unvalidated-other"])
                            report["rows"].append(row);save()
        report.update(status="passed",supported_rows=sum(row["supported"] for row in report["rows"]),unsupported_rows=sum(not row["supported"] for row in report["rows"]))
        with (out/"matrix.csv").open("w",newline="") as file:
            writer=csv.DictWriter(file,fieldnames=list(report["rows"][0]),lineterminator="\n");writer.writeheader();writer.writerows(report["rows"])
    except Exception as error:report.update(status="failed",error=str(error))
    finally:
        save();(out/"sha256.json").write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob("*")) if p.is_file() and p.name!="sha256.json"},indent=2)+"\n")
    print(json.dumps(dict(status=report["status"],rows=len(report["rows"]),scenarios=report["scenario_count"],error=report.get("error"))))
    return int(report["status"]!="passed")


if __name__=="__main__":sys.exit(main())
