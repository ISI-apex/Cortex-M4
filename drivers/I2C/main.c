
#include <stdio.h>
#include "dw_iic.h"
#include "dw_iic.h"
extern void dw_iic_all_install(void);
main()
{
	DEV_IIC_PTR dev0;
	int32_t result;
	char data[100];

	dev0 = iic_get_dev(DW_IIC_0_ID);
	printf("speed_mode = %d\n", dev0->iic_info.speed_mode);
	dev0->iic_open(DEV_MASTER_MODE, IIC_SPEED_STANDARD);
	dev0->iic_control(IIC_CMD_RESET, &result);
	dev0->iic_control(IIC_CMD_GET_STATUS, &result);
	printf("status = 0x%x\n", result);
	dev0->iic_read(data, 10);
}
