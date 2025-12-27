from pycdlib import PyCdlib
import os

HERE = os.path.dirname(__file__)
boot_img = os.path.join(HERE, '2ndboot64')
out_iso = os.path.join(HERE, 'bfree-64bit-el-torito.iso')

if not os.path.exists(boot_img):
    print('Error: boot image not found:', boot_img)
    raise SystemExit(1)

iso = PyCdlib()
iso.new(interchange_level=3, sys_ident='BFREE')
# add boot image file into /BOOT/BFREE.BIN;1
iso.add_file(boot_img, '/BOOT/BFREE.BIN;1')
# add El Torito boot entry (no emulation)
iso.add_eltorito_boot(boot_image='/BOOT/BFREE.BIN;1', boot_catalog='/BOOT/boot.cat;1', platform_id=0, emulation='no_emulation', boot_load_size=4, boot_info_table=True)
iso.write(out_iso)
iso.close()
print('ISO created:', out_iso)
