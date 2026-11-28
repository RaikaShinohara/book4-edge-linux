@echo -off
load %homefilesystem%\EFI\bookedge-el2-mst-hdmi\slbounce.efi
if not %lasterror% == 0 then
  echo "Could not load slbounce."
  exit
endif
%homefilesystem%\EFI\bookedge-el2-mst-hdmi\bookedge-el2-mst-hdmi.efi
