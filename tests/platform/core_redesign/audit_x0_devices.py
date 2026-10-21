#!/usr/bin/env python3
"""Lock device documents and calculate X0 protocol/period counterexamples; no MCU access."""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import urllib.request

ROOT=Path(__file__).resolve().parents[3]
INPUT=ROOT/'planning/core-redesign/x0-device-inputs.json'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdf-root',type=Path,help='Directory containing the locked id.pdf files')
    parser.add_argument('--upstream-root',type=Path,help='Root containing the locked upstream paths')
    parser.add_argument('--fetch',action='store_true',help='Explicitly fetch missing sources from fixed URLs')
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    config=json.loads(INPUT.read_text());report=dict(status='running',scope=__doc__,inputs={},sources=[],upstream_files=[],checks={},models={},execution=False);commands=[]
    def save():
        (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    def run(command):
        result=subprocess.run([str(x) for x in command],capture_output=True,text=True,timeout=90)
        commands.append(dict(command=[str(x) for x in command],returncode=result.returncode,stdout=result.stdout,stderr=result.stderr));save()
        assert result.returncode==0,result.stderr
        return result.stdout
    def obtain(item,relative,base):
        target=out/'sources'/relative;target.parent.mkdir(parents=True,exist_ok=True)
        if base:
            original=base.resolve()/relative;assert sha(original)==item['sha256'];shutil.copy2(original,target)
        else:
            assert args.fetch,'Specify local sources or --fetch'
            with urllib.request.urlopen(item['url'],timeout=45) as response:data=response.read()
            assert len(data)==item['bytes'] and hashlib.sha256(data).hexdigest()==item['sha256'];target.write_bytes(data)
        assert target.stat().st_size==item['bytes'] and sha(target)==item['sha256']
        return target
    save()
    try:
        for path in [INPUT,Path(__file__).resolve(),ROOT/config['golden']]:
            relative=path.relative_to(ROOT);target=out/'inputs'/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target);report['inputs'][str(relative)]=sha(path)
        assert sha(ROOT/config['golden'])==config['golden_sha256']
        tool=Path(shutil.which('pdftotext'));report['pdftotext_sha256']=sha(tool)
        run([tool,'-v']);report['pdftotext_version']=commands[-1]['stdout']+commands[-1]['stderr']
        documents={}
        for item in config['sources']:
            path=obtain(item,Path(item['id']+'.pdf'),args.pdf_root)
            assert path.read_bytes().startswith(b'%PDF-')
            text_path=path.with_suffix('.txt');run([tool,'-layout',path,text_path])
            assert sha(text_path)==item['text_sha256']
            pages=text_path.read_text().rstrip('\f').split('\f');assert len(pages)==item['pages']
            documents[item['id']]=pages;report['sources'].append(dict(item));save()
        for item in config['upstream_files']:
            path=obtain(item,Path(item['path']),args.upstream_root)
            assert hashlib.sha1(f'blob {path.stat().st_size}\0'.encode()+path.read_bytes()).hexdigest()==item['git_blob']
            report['upstream_files'].append(dict(item))
        ssd=documents['ssd1306-1.1'];ina=documents['ina219-g']
        for page,terms in [(19,['Co','D/C#']), (34,['Horizontal addressing mode','Vertical addressing mode'])]:
            assert all(term in ssd[page] for term in terms)
        assert all(term in ina[19] for term in ['68.10','128','SADC','BADC'])
        assert 'Status register' in ina[9] and 'convert pin' in ina[9] and 'Power Register' in ina[22]
        report['checks']['document_pages']=dict(ssd1306=[20,35],ina219=[10,20,23])
        upstream=(out/'sources/software/pd_adapter/src/ina219.h').read_text()
        word=int(re.search(r'^#define INA_CONFIG\s+(0b[01]+)',upstream,re.M)[1],2)
        calibration=int(re.search(r'^#define INA_CALIB\s+(\d+)',upstream,re.M)[1]);assert word==0x27ff and calibration==4096
        badc=(word>>7)&15;sadc=(word>>3)&15;mode=word&7;assert badc==sadc==15 and mode==7
        cycle=config['ina219_adc_code15_time_us']*2
        lsb=Fraction(4096,100000)/(calibration*Fraction(config['upstream_shunt_milliohms'],1000))
        report['models']['ina219']=dict(configuration=word,calibration=calibration,badc=badc,sadc=sadc,mode=mode,listed_time_per_quantity_us=config['ina219_adc_code15_time_us'],nominal_complete_cycle_us=cycle,nominal_complete_cycle_hz=float(Fraction(1000000,cycle)),declared_shunt_milliohms=config['upstream_shunt_milliohms'],current_lsb_ma=float(lsb*1000),cnvr_clear_interpretation='Unresolved document wording difference between pages 10 and 23; no read-clear policy chosen')
        for raw in range(65536):
            assert ((raw>>1)&0xfffc)==((raw>>3)*4)
            assert (raw if raw<32768 else raw-65536)==int.from_bytes(raw.to_bytes(2,'big'),'big',signed=True)
        report['checks']['raw_voltage_equivalence']=65536;report['checks']['signed_current_decode']=65536
        period=config['measurement_period_us'];window=config['phase_window_us'];histogram={}
        for phase in range(0,cycle,config['phase_grid_us']):
            previous=(0-phase)//cycle;fresh=0
            for t in range(period,window+1,period):
                current=(t-phase)//cycle;fresh+=int(current!=previous);previous=current
            histogram[fresh]=histogram.get(fresh,0)+1
        assert set(histogram)=={7,8} and sum(histogram.values())==cycle
        report['models']['read_opportunities']=dict(read_period_us=period,window_us=window,phases=len(range(0,cycle,config['phase_grid_us'])),fresh_complete_cycle_tag_histogram=histogram,reads_per_window=window//period)
        width=config['width'];pages=config['height']//8;total=width*pages;chunk=config['payload_bytes'];assert total==1024 and chunk==32
        golden=json.loads((ROOT/config['golden']).read_text())['frames'];image_rows=[]
        def place(stream,vertical):
            ram=[None]*total
            for i,value in enumerate(stream):ram[(i%pages)*width+i//pages if vertical else i]=value
            return ram
        header=[0,0x20,0,0x21,0,width-1,0x22,0,pages-1]
        chunk_headers=[]
        for offset in range(0,total,chunk):
            column=offset%width;page=offset//width;assert column+chunk<=width
            chunk_headers.append([0,0x20,0,0x21,column,column+chunk-1,0x22,page,page])
        labels=list(range(total));assert place(labels,False)==labels
        mismatch=sum(a!=b for a,b in zip(place(labels,True),labels));assert mismatch==1022
        for frame in golden:
            data=list(bytes.fromhex(frame['hex']));assert len(data)==total and hashlib.sha256(bytes(data)).hexdigest()==frame['sha256']
            reordered=[data[p*width+x] for x in range(width) for p in range(pages)]
            assert place(data,False)==data and place(reordered,True)==data and place(data,True)!=data
            for size in [1,16,31,32]:
                packets=[[0x40,*data[i:i+size]] for i in range(0,total,size)]
                stream=[v for packet in packets for v in packet[1:]];assert place(stream,False)==data
            ram=[None]*total
            for command,offset in zip(chunk_headers,range(0,total,chunk)):
                assert command[0:4]==[0,0x20,0,0x21] and command[6]==0x22 and command[7]==command[8]
                start=command[7]*width+command[4];size=command[5]-command[4]+1
                ram[start:start+size]=data[offset:offset+size]
            assert ram==data
            image_rows.append(dict(input=frame['input'],ram_sha256=frame['sha256'],vertical_unreordered_sha256=hashlib.sha256(bytes(place(data,True))).hexdigest(),vertical_reordered_stream_sha256=hashlib.sha256(bytes(reordered)).hexdigest()))
        report['models']['address_order']=dict(label_mismatches_with_unreordered_vertical=mismatch,golden_frames=len(golden),packet_payloads_checked=[1,16,31,32],images=image_rows)
        # These are candidate transaction layouts for arithmetic, not a chosen
        # initialization sequence, driver, or measured transfer duration.
        rows=[]
        for layout in ['frame-window','chunk-window']:
            commands=1 if layout=='frame-window' else total//chunk
            addressed_bytes=commands*(1+len(header))+(total//chunk)*(1+1+chunk)
            for hz in config['bus_rates_hz']:
                rows.append(dict(layout=layout,bus_hz=hz,payload_bytes=total,window_commands=commands,data_transactions=total//chunk,transaction_count=commands+total//chunk,address_and_control_and_data_bytes=addressed_bytes,scl_clocks=addressed_bytes*9,ideal_clock_only_us=float(Fraction(addressed_bytes*9*1000000,hz))))
        assert rows[0]['scl_clocks']==9882 and rows[2]['scl_clocks']==12672
        report['models']['display_wire']=dict(frame_window_command=header,chunk_window_commands=chunk_headers,data_control=0x40,required_data_packet_bytes=chunk+1,rows=rows)
        report['status']='passed'
    except Exception as error:report.update(status='failed',error=str(error))
    finally:
        save();(out/'sha256.json').write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file() and p.name!='sha256.json'},indent=2)+'\n')
    print(json.dumps(dict(status=report['status'],checks=report['checks'],error=report.get('error'))));return int(report['status']!='passed')


if __name__=='__main__':sys.exit(main())
