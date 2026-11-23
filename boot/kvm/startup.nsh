@echo -off
load %homefilesystem%\EFI\bookedge-kvm-uki\slbounce.efi
if not %lasterror% == 0 then
  echo "Could not load slbounce."
  exit
endif
%homefilesystem%\EFI\bookedge-kvm-uki\bookedge-kvm-uki.efi
