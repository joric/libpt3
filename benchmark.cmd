@echo off
if not exist test mkdir test

pt3_reader_shiru.exe >test/data_shiru.txt
pt3_reader_zxssk.exe >test/data_zxssk.txt

py txt_to_h.py test/data_shiru.txt >test/data_shiru.h
py txt_to_h.py test/data_zxssk.txt >test/data_zxssk.h

ay_render_shiru.exe
ay_render_ayemul.exe
