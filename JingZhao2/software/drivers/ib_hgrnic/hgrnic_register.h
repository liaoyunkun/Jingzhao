/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_register.h
 * @note Provide interface for ethernet driver registering
 *************************************************************/

#ifndef __HGRNIC_REGISTER_H__
#define __HGRNIC_REGISTER_H__

#include "hgrnic_dev.h"

int hgrnic_init_en_register (struct hgrnic_dev* dev);


#endif // __HGRNIC_REGISTER_H__