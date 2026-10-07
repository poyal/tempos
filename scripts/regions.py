"""Convert the official KMA region workbook to a bounded UTF-8 region index.
Usage: python scripts/regions.py artifacts/kma-regions-260701.xlsx
"""
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET
import zipfile

ns={'m':'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
with zipfile.ZipFile(sys.argv[1]) as archive:
    strings=[''.join(t.itertext()) for t in ET.fromstring(archive.read('xl/sharedStrings.xml')).findall('m:si',ns)]
    rows=ET.fromstring(archive.read('xl/worksheets/sheet1.xml')).findall('m:sheetData/m:row',ns)
    regions={}
    for row in rows[1:]:
        values={}
        for cell in row:
            column=''.join(c for c in cell.get('r','') if c.isalpha())
            value=cell.find('m:v',ns)
            values[column]=(strings[int(value.text)] if cell.get('t')=='s' else value.text) if value is not None else ''
        if values.get('A')!='kor': continue
        name=' '.join(values.get(c,'').strip() for c in ('C','D','E') if values.get(c,'').strip())
        try:
            region=dict(name=name,nx=int(values['F']),ny=int(values['G']),lon=round(float(values['N']),6),lat=round(float(values['O']),6))
            if name and 1<=region['nx']<=149 and 1<=region['ny']<=253: regions[name]=region
        except (KeyError,ValueError): pass
Path('data/regions.json').write_text(json.dumps(list(regions.values()),ensure_ascii=False,separators=(',',':')),encoding='utf-8')
print(f'{len(regions)} official regions')
