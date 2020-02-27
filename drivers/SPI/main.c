

#include "dw_spi.h"
//#include "dw_spi_obj.h"
extern void dw_iic_all_install(void);
main()
{
	DEV_SPI_PTR dev0;
	int32_t result;

	dev0 = spi_get_dev(DW_SPI_0_ID);
	dev0->spi_open(DEV_MASTER_MODE, SPI_CPOL_0_CPHA_0);
	dev0->spi_control(SPI_CMD_GET_STATUS, &result);
	printf("status = 0x%x\n", result);
}
