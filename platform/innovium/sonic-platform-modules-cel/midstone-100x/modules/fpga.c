#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/hwmon.h>
#include <linux/acpi.h>

#include "xcvr-cls.h"
#include "fpga.h"


#define FPGA_VERSION         0x0000
#define TEST_REG             0x0004

struct fpga_data{
	struct fpga_priv *fpga;
	const char *name;
	unsigned int index;
};

struct fpga_priv {
	struct device* dev;
	void __iomem *base;
	struct mutex fpga_lock;  
	int fpga_reg_size;
	int num_ports;
	void __iomem *fpga_re_addr;
	struct device **fp_devs;
};

static inline void port_setreg(struct fpga_priv *fpga, int reg, u8 value)
{
	return iowrite8(value, fpga->base + reg );
}

static inline u8 port_getreg(struct fpga_priv *fpga, int reg)
{
	return ioread8(fpga->base + reg );
}

static inline void port_setreg32(struct fpga_priv *fpga, int reg, int value)
{
	return iowrite32(value, fpga->base + reg);
}

static inline int port_getreg32(struct fpga_priv *fpga, int reg)
{
	return ioread32(fpga->base + reg);
}



static ssize_t fpga_version_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int data;
	struct fpga_priv *fpga = dev_get_drvdata(dev);
	data = port_getreg32(fpga, FPGA_VERSION);
	return sprintf(buf, "0x%8.8x\n", data & 0xFFFFFFFF);
}
				  

static ssize_t scratch_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int data;
	struct fpga_priv *fpga = dev_get_drvdata(dev);
	

	data = port_getreg32(fpga, TEST_REG);
	return sprintf(buf, "0x%8.8x\n", data & 0xFFFFFFFF);
}

static ssize_t scratch_store(struct device *dev, 
				   struct device_attribute *attr, 
				   const char *buf, size_t size)
{

	long value;
	char  *nplast;
	struct fpga_priv *fpga = dev_get_drvdata(dev);

	value =(uint32_t)strtoul(buf, &nplast, 16);
	
	port_setreg32(fpga, TEST_REG, value);

	return size;
}


static ssize_t getreg_show(struct device *dev, struct device_attribute *devattr,
                                   char *buf)
{
    // read data from the address
    uint32_t data;
    struct fpga_priv *fpga = dev_get_drvdata(dev);
    data = ioread8(fpga->fpga_re_addr);
    return sprintf(buf, "0x%8.8x\n", data);
}

static ssize_t getreg_store(struct device *dev, struct device_attribute *attr,
                                      const char *buf, size_t size)
{
     uint32_t addr;
     char *last;
     struct fpga_priv *fpga = dev_get_drvdata(dev);
     addr = (uint32_t)strtoul(buf, &last, 16);
     if (addr == 0 && buf == last) {
         return -EINVAL;
     }
     fpga->fpga_re_addr = fpga->base + addr;
     return size;
}

static ssize_t setreg_store(struct device *dev, struct device_attribute *devattr,
                                  const char *buf, size_t count)
{
    // register are 4 bytes
    uint32_t addr;
    uint32_t value;
    uint32_t mode = 8;
    char *tok;
    char clone[count];
    char *pclone = clone;
    char *last;
	struct fpga_priv *fpga = dev_get_drvdata(dev);
	
    strcpy(clone, buf);
    mutex_lock(&fpga->fpga_lock);
    tok = strsep((char**)&pclone, " ");
    if (tok == NULL) {
        mutex_unlock(&fpga->fpga_lock);
        return -EINVAL;
    }
    addr = (uint32_t)strtoul(tok, &last, 16);
    if (addr == 0 && tok == last) {
        mutex_unlock(&fpga->fpga_lock);
        return -EINVAL;
    }
    tok = strsep((char**)&pclone, " ");
    if (tok == NULL) {
        mutex_unlock(&fpga->fpga_lock);
        return -EINVAL;
    }
    value = (uint32_t)strtoul(tok, &last, 16);
    if (value == 0 && tok == last) {
        mutex_unlock(&fpga->fpga_lock);
        return -EINVAL;
    }
    tok = strsep((char**)&pclone, " ");
    if (tok == NULL) {
        mode = 32;
    } else {
        mode = (uint32_t)strtoul(tok, &last, 10);
        if (mode == 0 && tok == last) {
            mutex_unlock(&fpga->fpga_lock);
            return -EINVAL;
        }
    }
    if (mode == 32) {
        iowrite32(value, fpga->base + addr);
    } else if (mode == 8) {
        iowrite8(value, fpga->base + addr);
    } else {
        mutex_unlock(&fpga->fpga_lock);
        return -EINVAL;
    }
    mutex_unlock(&fpga->fpga_lock);
    return count;
}


DEVICE_ATTR_RO(fpga_version);
DEVICE_ATTR_RW(scratch);
DEVICE_ATTR_RW(getreg);
//DEVICE_ATTR_WO(fpga_set_addr);
DEVICE_ATTR_WO(setreg);



static struct attribute *fpga_attrs[] = {
	&dev_attr_fpga_version.attr,
	&dev_attr_scratch.attr,
	&dev_attr_getreg.attr,
//	&dev_attr_fpga_set_addr.attr,
	&dev_attr_setreg.attr,
	NULL
};

ATTRIBUTE_GROUPS(fpga);


static struct device* init_port(struct device *dev, 
				struct fpga_priv *fpga, 
				struct fpga_info info, 
				const struct attribute_group **groups) 
{
	struct fpga_data *new_data;

	new_data = devm_kzalloc(dev, sizeof(struct fpga_data), GFP_KERNEL);
	if (!new_data)
		return ERR_PTR(-ENOMEM);

	new_data->index = info.index;
	new_data->name = info.name;
	new_data->fpga = fpga;

	return devm_hwmon_device_register_with_groups(dev, 
						      info.name, 
						      new_data->fpga, 
						      groups);
}

static void fpga_cleanup(struct fpga_priv *fpga)
{
	struct device *dev;
	struct fpga_data *data;
	int i;

	for (i = 0; i < fpga->num_ports; i++){
		dev = fpga->fp_devs[i];
		if (dev == NULL)
			continue;
		data = dev_get_drvdata(dev);
		sysfs_remove_link(&fpga->dev->kobj, data->name);
	}
}

static int fpga_probe(struct platform_device *pdev)
{

	struct fpga_priv *fpga;
	struct fpga_platform_data *pdata;
	struct resource *res;
	int ret;
	int i;

	struct device **port_devs;

	fpga = devm_kzalloc(&pdev->dev, sizeof(struct fpga_priv), GFP_KERNEL);
	if (!fpga){
		ret = -ENOMEM;
		goto err_exit;
	}
	mutex_init(&fpga->fpga_lock);
	dev_set_drvdata(&pdev->dev, fpga);

	/* mmap resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res) {
		fpga->base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(fpga->base)){
			ret = PTR_ERR(fpga->base);
			goto err_exit;
		}
	}

	pdata = dev_get_platdata(&pdev->dev);
	fpga->dev = &pdev->dev;

	if (pdata) {
		/* assign pdata */
		fpga->num_ports = pdata->num_ports;
		fpga->fpga_reg_size = pdata->fpga_reg_size;
	}

	/* alloc front panel device list */
	port_devs = devm_kzalloc(&pdev->dev, 
				 fpga->num_ports * sizeof(struct device*), 
				 GFP_KERNEL);
	if (!port_devs){
		ret = -ENOMEM;
		goto err_exit;
	}


	if (pdata) {
		/* create each device attrs group determined by type */
		for (i = 0; i < pdata->num_ports; i++) {
			struct device *fp_dev;


			fp_dev = init_port(&pdev->dev,
					   fpga, 
					   pdata->devices[i], 
					   fpga_groups);

			if (IS_ERR(fp_dev)) {
				dev_err(&pdev->dev, 
					"Failed to init port %s\n", 
					pdata->devices[i].name);
				ret = PTR_ERR(fp_dev);
				goto dev_clean_up;
			}

			dev_info(&pdev->dev, 
				 "Register port %s\n", 
				 pdata->devices[i].name);

			WARN(sysfs_create_link(&pdev->dev.kobj, 
					       &fp_dev->kobj, 
					       pdata->devices[i].name),
			     "can't create symlink to %s\n", pdata->devices[i].name);
			port_devs[i] = fp_dev;
			fp_dev = NULL;
		}
		fpga->fp_devs = port_devs;
	}

	return 0;

dev_clean_up:
	fpga_cleanup(fpga);
err_exit:
	return ret;

}


static int fpga_remove(struct platform_device *pdev)
{
	struct fpga_priv *fpga = dev_get_drvdata(&pdev->dev);
	fpga_cleanup(fpga);
	return 0;
}


static struct platform_driver fpga_driver_lc = {
	.probe = fpga_probe,
	.remove = fpga_remove,
	.driver = {
		.name = "fpga-board",
	},
};


static int __init drv_init(void)
{
	int rc = 0;
	rc = platform_driver_register(&fpga_driver_lc);
	return rc; 
}

static void __exit drv_exit(void)
{
    platform_driver_unregister(&fpga_driver_lc);
}

module_init(drv_init);
module_exit(drv_exit);

MODULE_AUTHOR("Pradchaya Phucharoen<pphuchar@celestica.com>");
MODULE_DESCRIPTION("Celestica xcvr control driver");
MODULE_VERSION("0.0.1-3");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cls-xcvr");


