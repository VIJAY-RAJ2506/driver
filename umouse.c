#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <uapi/linux/hid.h>
#include <linux/bits.h>
#include <linux/mod_devicetable.h>

MODULE_AUTHOR("VIJAY<mvr250697@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB mouse driver for Dell Optical mouse");

#ifndef VENDOR_ID
#define VENDOR_ID 0x413c
#endif

#ifndef PRODUCT_ID 
#define PRODUCT_ID 0x301a
#endif

#ifndef DEV_NAME
#define DEV_NAME "Mouse_driver"
#endif

static struct usb_device_id m_table[] = {
	{USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID,USB_INTERFACE_SUBCLASS_BOOT,USB_INTERFACE_PROTOCOL_MOUSE)},
	{}

};

MODULE_DEVICE_TABLE(usb,m_table);

struct mouse {

	char *data;
	char phy[64];
	struct urb *urb;
	struct input_dev *idev;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_host_interface *iface;
	struct usb_device *device;
	dma_addr_t data_dma;
};

static int mouse_open(struct input_dev *idev) {

	struct mouse *dev=input_get_drvdata(idev);
	if(usb_submit_urb(dev->urb,GFP_KERNEL))
		return -EIO;


	return 0;
}

static void mouse_close(struct input_dev *idev) {
	
	struct mouse *dev = input_get_drvdata(idev);
	
	usb_kill_urb(dev->urb);
}

void mouse_data(struct urb *urb) {
	
	struct mouse *mdev=urb->context;
	struct input_dev *idev=mdev->idev;
	signed char *data= mdev->data;

	if (urb->status && !(urb->status == -ENOENT || urb->status == -ECONNRESET || urb->status == -ESHUTDOWN)) {
		
		printk(KERN_INFO "urb status received :%d \n",urb->status);
	}
	
	input_report_key(idev,BTN_LEFT,data[0] & 0x01);
	input_report_key(idev,BTN_RIGHT,data[0] & 0x02);
	input_report_key(idev,BTN_MIDDLE,data[0] & 0x04);

	input_report_rel(idev,REL_X,data[1]);
	input_report_rel(idev,REL_Y,data[2]);
	input_report_rel(idev,REL_WHEEL,data[3]);

	input_sync(idev);

}

int mouse_usb_probe(struct usb_interface *interface, const struct usb_device_id *id)  {

	int i;
	int pipe,maxp;
	struct usb_host_interface *iface;
	struct mouse *mdev;
	struct input_dev *idev;
	
	iface=interface->cur_altsetting;

	mdev=(struct mouse *)kmalloc(sizeof(struct mouse),GFP_KERNEL);
	if(!mdev) {
		printk(KERN_ERR "MDEV FAILED \n");
		return -1;
	}
	
	mdev->iface=iface;

	if(iface->desc.bNumEndpoints) {
		for(i=0;i<iface->desc.bNumEndpoints;++i) {
				mdev->endpoint=&iface->endpoint[i].desc;
			if(((mdev->endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT) && (mdev->endpoint->bEndpointAddress & USB_DIR_IN)) 
				break;
		}
	}
	mdev->device=interface_to_usbdev(interface);
	if(!mdev->device) {
		printk(KERN_ERR "failed to acquire usb_device \n");
		return -1;
	}


	pipe=usb_rcvintpipe(mdev->device,mdev->endpoint->bEndpointAddress);
	maxp=usb_maxpacket(mdev->device,pipe,usb_pipeout(pipe));

	mdev->data=usb_alloc_coherent(mdev->device,8,GFP_ATOMIC,&mdev->data_dma);
	if(!mdev->data) {
		printk(KERN_ERR "usb_alloc_coherent failed \n");
		return -1;
	}

	mdev->urb=usb_alloc_urb(0,GFP_KERNEL);	
	if(!mdev->urb) {
		printk(KERN_ERR "Urb allocation failed \n");
		usb_free_coherent(mdev->device,8,mdev->data,mdev->data_dma);
		return -1;
	}
	
	mdev->idev=input_allocate_device();
	if(!mdev->idev)
	{
			
		usb_free_urb(mdev->urb);
		return -1;
	}
		
	idev=mdev->idev;

	idev->name=DEV_NAME;
	usb_make_path(mdev->device,mdev->phy,sizeof(mdev->phy));
	strlcat(mdev->phy,"/input0",sizeof(mdev->phy));
//	idev->phys="/home/vijay/driver/mouse/data"; //desired physical path
	usb_to_input_id(mdev->device,&idev->id);	
	idev->dev.parent=&interface->dev;
	
	idev->evbit[0]= BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	idev->keybit[BIT_WORD(BTN_MOUSE)]= BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);
	idev->relbit[0]= BIT_MASK(REL_X) | BIT_MASK(REL_Y);
	idev->relbit[0] |= BIT_MASK(REL_WHEEL);

	input_set_drvdata(idev,mdev);

	idev->open=mouse_open;
	idev->close=mouse_close;

	usb_set_intfdata(interface,(void*)mdev);
	
	printk(KERN_INFO "INTERVAL VAUE : %d \n",mdev->endpoint->bInterval);

	usb_fill_int_urb(mdev->urb,mdev->device,pipe,mdev->data,(maxp > 8?8 : maxp),mouse_data,mdev,mdev->endpoint->bInterval);
		
	mdev->urb->transfer_dma=mdev->data_dma;
	mdev->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	if(input_register_device(mdev->idev)) {
		usb_free_urb(mdev->urb);
	}

	return 0;
}

void mouse_usb_disconnect(struct usb_interface *interface)
{
	struct mouse *mdev;

	mdev=(struct mouse *)usb_get_intfdata(interface);
	
	usb_kill_urb(mdev->urb);
	input_unregister_device(mdev->idev);
	usb_free_urb(mdev->urb);
	usb_free_coherent(interface_to_usbdev(interface),8,mdev->data,mdev->data_dma);
	kfree(mdev);

}
static struct usb_driver m_driver = {
	.name="mouse_driver",
	.probe=mouse_usb_probe,
	.disconnect=mouse_usb_disconnect,
	.id_table=m_table
};

static int __init init_func(void) {
	
	if(usb_register(&m_driver) == -1) {
		printk(KERN_ERR "usb_register failed \n");
		return -1;
	}

	return 0;
}


static void __exit clean_func(void) {
	usb_deregister(&m_driver);

}

module_init(init_func);
module_exit(clean_func);

