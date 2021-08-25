#!/usr/bin/env python

#############################################################################
# Celestica
#
# Component contains an implementation of SONiC Platform Base API and
# provides the components firmware management function
#
#############################################################################

import os.path
import shutil
import subprocess

try:
    from sonic_platform_base.component_base import ComponentBase
    from helper import APIHelper
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

FPGA_VERSION_PATH = "/sys/devices/platform/fpga-sys/version"
SYSCPLD_VERSION_PATH = "/sys/devices/platform/sys_cpld/version"
SWCPLD1_VERSION_PATH = "/sys/bus/i2c/devices/i2c-10/10-0030/version"
SWCPLD2_VERSION_PATH = "/sys/bus/i2c/devices/i2c-10/10-0031/version"
BIOS_VERSION_PATH = "/sys/class/dmi/id/bios_version"
Main_BMC_cmd = "ipmitool raw 0x32 0x8f 0x08 0x01"
Backup_BMC_cmd = "ipmitool raw 0x32 0x8f 0x08 0x02"
Fan_CPLD_cmd = "ipmitool raw 0x3a 0x64 02 01 00"
COMPONENT_NAME_LIST = ["FPGA", "SYSCPLD", "SWCPLD1", "SWCPLD2", "FANCPLD", "Main_BMC", "Backup_BMC", "BIOS"]
COMPONENT_DES_LIST = ["Used for managering the CPU and expanding I2C channels", "Used for managing the CPU",
                      "Used for managing QSFP+ ports (1-16)", "Used for managing QSFP+ ports (17-32)", "Used for managing fans", "Main Baseboard Management Controller", "Backup Baseboard Management Controller", "Basic Input/Output System"]


class Component(ComponentBase):
    """Platform-specific Component class"""

    DEVICE_TYPE = "component"

    def __init__(self, component_index):
        ComponentBase.__init__(self)
        self.index = component_index
        self._api_helper = APIHelper()
        self.name = self.get_name()

    def __get_bios_version(self):
        # Retrieves the BIOS firmware version
        try:
            with open(BIOS_VERSION_PATH, 'r') as fd:
                bios_version = fd.read()
                return bios_version.strip()
        except Exception as e:
            return None

    def get_register_value(self, register):
        # Retrieves the cpld register value
        cmd = "echo {1} > {0}; cat {0}".format(GETREG_PATH, register)
        p = subprocess.Popen(
            cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        raw_data, err = p.communicate()
        if err is not '':
            return None
        return raw_data.strip()

    def __get_cpld_version(self):
        if self.name == "SYSCPLD":
            try:
                with open(SYSCPLD_VERSION_PATH, 'r') as fd:
                    syscpld_version = fd.read()
                    return syscpld_version.strip()
            except Exception as e:
                return None
        elif self.name == "SWCPLD1":
            try:
                with open(SWCPLD1_VERSION_PATH, 'r') as fd:
                    swcpld1_version = fd.read()
                    return swcpld1_version.strip()
            except Exception as e:
                return None
        elif self.name == "SWCPLD2":
            try:
                with open(SWCPLD2_VERSION_PATH, 'r') as fd:
                    swcpld2_version = fd.read()
                    return swcpld2_version.strip()
            except Exception as e:
                return None
        elif self.name == "FANCPLD":
            status,ver = self._api_helper.run_command(Fan_CPLD_cmd)
            version = int(ver.strip(), 16)
            return version
 
    def __get_fpga_version(self):
            # Retrieves the FPGA firmware version
            try:
                with open(FPGA_VERSION_PATH, 'r') as fd:
                    version = fd.read()
                    fpga_version = (version.strip().split("x")[1])
                    return fpga_version.strip()
            except Exception as e:
                return None

    def __get_bmc_version(self):
            # Retrieves the BMC firmware version
            cmd = Main_BMC_cmd if self.name == "Main_BMC" else Backup_BMC_cmd
            stasus, ver = self._api_helper.run_command(cmd)
            return ver.strip()
 
    def get_name(self):
        """
        Retrieves the name of the component
         Returns:
            A string containing the name of the component
        """
        return COMPONENT_NAME_LIST[self.index]

    def get_description(self):
        """
        Retrieves the description of the component
            Returns:
            A string containing the description of the component
        """
        return COMPONENT_DES_LIST[self.index]

    def get_firmware_version(self):
        """
        Retrieves the firmware version of module
        Returns:
            string: The firmware versions of the module
        """
        fw_version = None
 
        if self.name == "BIOS":
            fw_version = self.__get_bios_version()
        elif "CPLD" in self.name:
            fw_version = self.__get_cpld_version()
        elif self.name == "FPGA":
            fw_version = self.__get_fpga_version()
        elif "BMC" in self.name:
            version = self.__get_bmc_version()
            version_1 = int(version.strip().split(" ")[0])
            version_2 = int(version.strip().split(" ")[1], 16)
            fw_version = "%s.%s" % (version_1, version_2)

        return fw_version

    def install_firmware(self, image_path):
        """
        Install firmware to module
        Args:
            image_path: A string, path to firmware image
        Returns:
            A boolean, True if install successfully, False if not
        """
        if not os.path.isfile(image_path):
            return False

        if "CPLD" in self.name:
            img_name = os.path.basename(image_path)
            root, ext = os.path.splitext(img_name)
            ext = ".vme" if ext == "" else ext
            new_image_path = os.path.join("/tmp", (root.lower() + ext))
            shutil.copy(image_path, new_image_path)
            install_command = "ispvm %s" % new_image_path
        # elif self.name == "BIOS":
        #     install_command = "afulnx_64 %s /p /b /n /x /r" % image_path

        return self.__run_command(install_command)
