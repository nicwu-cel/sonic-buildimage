# Platform drivers for Celestica Midstone-200i for the SONiC project
# Platform drivers for Celestica Midstone100x and Silverstonex for the SONiC project
## Litmitation

Because SONiC 201911 branch does not support kernel options setting for each platform, 
but SilverstoneX and Midstone100x kernel is required to set "CONFIG_SCSI_PROC_FS=y" to open USB upgrade channel for BMC upgrade(such as BMC, BIOS and CPLD) by using "ipmitool -I usb". 
Add below steps to fix this temporarily until we find a better way.
    Add below 3 lines after "fakeroot make -f debian/rules.gen setup_amd64_none_amd64" in $SONIC/src/sonic-linux-kernel/Makefile after "make init" before "make configure PLATFORM=[ASIC_VENDOR]"

    # add kernel option to support ipmitool command via usb on SONiC
    echo "CONFIG_SCSI_PROC_FS=y" >> debian/build/build_amd64_none_amd64/.config
    make -C debian/build/build_amd64_none_amd64/ olddefconfig

