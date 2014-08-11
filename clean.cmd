attrib *.suo -r -a -s -h /s /d
attrib *.ncb -r -a -s -h /s /d
attrib *.user -r -a -s -h /s /d
del DirectQ.suo /q
del DirectQ.ncb /q
cd DirectQ
del *.user /q
rmdir Debug /s /q
rmdir Release /s /q
cd ..
echo done
pause
