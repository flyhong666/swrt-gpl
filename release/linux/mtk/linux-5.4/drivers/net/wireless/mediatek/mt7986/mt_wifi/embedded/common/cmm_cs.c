/*
 * Copyright (c) [2020], MediaTek Inc. All rights reserved.
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws.
 * The information contained herein is confidential and proprietary to
 * MediaTek Inc. and/or its licensors.
 * Except as otherwise provided in the applicable licensing terms with
 * MediaTek Inc. and/or its licensors, any reproduction, modification, use or
 * disclosure of MediaTek Software, and information contained herein, in whole
 * or in part, shall be strictly prohibited.
*/
/*
 ***************************************************************************
 ***************************************************************************

    Module Name:
    cmm_cs.c

    Abstract:
    Carrier Sensing related functions

    Revision History:
    Who       When            What
    ---------------------------------------------------------------------
*/
#include "rt_config.h"

#ifdef CARRIER_DETECTION_SUPPORT
static ULONG time[20];
static ULONG idle[20];
static ULONG busy[20];
static ULONG cd_idx;

static void ToneRadarProgram(PRTMP_ADAPTER pAd);

#ifdef CONFIG_AP_SUPPORT
static inline VOID CarrierDetectionResetStatus(PRTMP_ADAPTER pAd)
{
	struct _RTMP_CHIP_CAP *cap = hc_get_chip_cap(pAd->hdev_ctrl);

	if (cap->carrier_func == TONE_RADAR_V3)
		RTMP_BBP_IO_WRITE32(pAd, TR_R1, 0x3);
	else
		RTMP_CARRIER_IO_WRITE8(pAd, 1, 1);
}

static inline VOID CarrierDetectionStatusGet(PRTMP_ADAPTER pAd,
		PUINT8 pStatus)
{
	struct _RTMP_CHIP_CAP *cap = hc_get_chip_cap(pAd->hdev_ctrl);
	*pStatus = 0;

	if (cap->carrier_func == TONE_RADAR_V3) {
		UINT32 mac_value = 0;
		RTMP_BBP_IO_READ32(pAd, TR_R1, &mac_value);
		*pStatus = (UINT8) mac_value;
	} else
		RTMP_CARRIER_IO_READ8(pAd, 1, pStatus);
}

static inline VOID CarrierDetectionEnable(PRTMP_ADAPTER pAd,
		BOOLEAN bEnable)
{
	struct _RTMP_CHIP_CAP *cap = hc_get_chip_cap(pAd->hdev_ctrl);

	if (cap->carrier_func == TONE_RADAR_V3)
		RTMP_BBP_IO_WRITE32(pAd, TR_R0, bEnable);
	else
		RTMP_CARRIER_IO_WRITE8(pAd, 0, bEnable);
}


/*
    ==========================================================================
    Description:
	Check current CS state, indicating Silient state (carrier exist) or not
	Arguments:
	    pAd                    Pointer to our adapter

    Return Value:
	TRUE if the current state is SILENT state, FALSE other wise
    Note:
    ==========================================================================
*/
INT isCarrierDetectExist(
	IN PRTMP_ADAPTER pAd)
{
	if (pAd->CommonCfg.CarrierDetect.CD_State == CD_SILENCE)
		return TRUE;
	else
		return FALSE;
}

/*
    ==========================================================================
    Description:
	Enable or Disable Carrier Detection feature (AP ioctl).
	Arguments:
	    pAd                    Pointer to our adapter
	    arg                     Pointer to the ioctl argument

    Return Value:
	None

    Note:
	Usage:
	       1.) iwpriv ra0 set CarrierDetect=[1/0]
    ==========================================================================
*/
INT Set_CarrierDetect_Proc(RTMP_ADAPTER *pAd, RTMP_STRING *arg)
{
	POS_COOKIE pObj = (POS_COOKIE) pAd->OS_Cookie;
	UCHAR apidx = pObj->ioctl_if;
	UINT Enable;

	if (apidx != MAIN_MBSSID)
		return FALSE;

	Enable = (UINT) os_str_tol(arg, 0, 10);
	pAd->CommonCfg.CarrierDetect.Enable = (BOOLEAN)(Enable == 0 ? FALSE : TRUE);
	RTMP_CHIP_RADAR_GLRT_COMPENSATE(pAd);
	RTMP_CHIP_CCK_MRC_STATUS_CTRL(pAd);

	if (pAd->CommonCfg.CarrierDetect.Enable == TRUE)
		CarrierDetectionStart(pAd);
	else
		CarrierDetectionStop(pAd);

	MTWF_LOG(DBG_CAT_HW, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("%s:: %s\n", __func__,
			 pAd->CommonCfg.CarrierDetect.Enable == TRUE ? "Enable Carrier Detection" : "Disable Carrier Detection"));
	return TRUE;
}
#endif /* CONFIG_AP_SUPPORT */


/*
    ==========================================================================
    Description:
	When h/w interrupt is not available for CS, this function monitor necessary parameters that determine the CS
	state periodically. (every 100ms)

	Arguments:
	    pAd                    Pointer to our adapter

    Return Value:
	None

    Note:
    ==========================================================================
*/
VOID CarrierDetectionPeriodicStateCtrl(
	IN PRTMP_ADAPTER pAd)
{
	PCARRIER_DETECTION_STRUCT pCarrierDetect = &pAd->CommonCfg.CarrierDetect;
	CD_STATE *pCD_State = &pCarrierDetect->CD_State;
	ULONG *pOneSecIntCount = &pCarrierDetect->OneSecIntCount;
	struct _RTMP_CHIP_CAP *cap = hc_get_chip_cap(pAd->hdev_ctrl);

#ifdef CONFIG_ATE

	/* Nothing to do in ATE mode */
	if (ATE_ON(pAd))
		return;

#endif /* CONFIG_ATE */

	/* TODO: shiang-7603 */
	if (IS_HIF_TYPE(pAd, HIF_MT)) {
		MTWF_DBG(pAd, DBG_CAT_HW, DBG_SUBCAT_ALL, DBG_LVL_ERROR, "Not support for HIF_MT yet!\n");
		return;
	}

	if (pCarrierDetect->bCsInit == FALSE)
		return;

	if (cap->carrier_func == TONE_RADAR_V3) {
		UINT8 TrStatus = 0;
		CarrierDetectionStatusGet(pAd, &TrStatus);

		if (TrStatus) {
			if (*pCD_State == CD_NORMAL) {
				MTWF_DBG(pAd, DBG_CAT_HW, DBG_SUBCAT_ALL, DBG_LVL_TRACE,
						 "Carrier Detected ! (TrStatus = 0x%x)\n", TrStatus);
				*pCD_State = CD_SILENCE;
				/* stop all TX actions including Beacon sending.*/
				AsicDisableSync(pAd, HW_BSSID_0);
			}

			*pOneSecIntCount  = pCarrierDetect->CarrierGoneThreshold;
			CarrierDetectionResetStatus(pAd);
		} else
			*pOneSecIntCount  = 0;

		/*CarrierDetectionResetStatus(pAd);*/
	}
}


/*
    ==========================================================================
    Description:
	When there is no f/w taking care of CS operation, this function depends on h/w interrupts for every possible carrier
	tone to judge the CS state

	Arguments:
	    pAd                    Pointer to our adapter

    Return Value:
	None

    Note:
    ==========================================================================
*/
VOID RTMPHandleRadarInterrupt(PRTMP_ADAPTER  pAd)
{
	UINT32 value, delta;
	UCHAR bbp = 0;
	PCARRIER_DETECTION_STRUCT pCarrierDetect = &pAd->CommonCfg.CarrierDetect;
	struct _RTMP_CHIP_CAP *cap = hc_get_chip_cap(pAd->hdev_ctrl);

	if (cap->carrier_func > TONE_RADAR_V2)
		return;

	MTWF_LOG(DBG_CAT_HW, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("RTMPHandleRadarInterrupt()\n"));
	RTMP_IO_READ32(pAd->hdev_ctrl, PBF_LIFE_TIMER, &value);
	RTMP_IO_READ32(pAd->hdev_ctrl, CH_IDLE_STA, &pCarrierDetect->idle_time);
	pCarrierDetect->busy_time = AsicGetChBusyCnt(pAd, 0);
	delta = (value >> 4) - pCarrierDetect->TimeStamp;
	pCarrierDetect->TimeStamp = value >> 4;
	pCarrierDetect->OneSecIntCount++;

	if (cap->carrier_func == TONE_RADAR_V2) {
		{
			UINT32 RadarInt = 0;
			/* Disable carrier detection and clear the status bit*/
			CarrierDetectionEnable(pAd, 0);
			CarrierDetectionResetStatus(pAd);
			/* Clear interrupt */
			RTMP_IO_WRITE32(pAd->hdev_ctrl, INT_SOURCE_CSR, RadarInt);
		}
	}

	if (pCarrierDetect->Debug) {
		if (cd_idx < 20) {
			time[cd_idx] = delta;
			idle[cd_idx] = pCarrierDetect->idle_time;
			busy[cd_idx] = pCarrierDetect->busy_time;
			cd_idx++;
		} else {
			int i;
			pCarrierDetect->Debug = 0;

			for (i = 0; i < 20; i++)
				printk("%3d %4ld %ld %ld\n", i, time[i], idle[i], busy[i]);

			cd_idx = 0;
		}
	}

	if (pCarrierDetect->CD_State == CD_NORMAL) {
		if ((delta < pCarrierDetect->criteria) && (pCarrierDetect->recheck))
			pCarrierDetect->recheck--;
		else
			pCarrierDetect->recheck = pCarrierDetect->recheck1;

		if (pCarrierDetect->recheck == 0) {
			/* declare carrier sense*/
			pCarrierDetect->CD_State = CD_SILENCE;

			if (pCarrierDetect->Debug != DBG_LVL_TRACE) {
				MTWF_LOG(DBG_CAT_HW, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("Carrier Detected\n"));
				/* stop all TX actions including Beacon sending.*/
				AsicDisableSync(pAd, HW_BSSID_0);
			} else
				printk("Carrier Detected\n");
		}
	}

	if (cap->carrier_func == TONE_RADAR_V2) {
		CarrierDetectionStatusGet(pAd, &bbp);

		if (bbp & 0x1) {
			MTWF_DBG(pAd, DBG_CAT_HW, DBG_SUBCAT_ALL, DBG_LVL_WARN, "CS bit not cleared!!!\n");
			CarrierDetectionResetStatus(pAd);
		}

		/* re-enable carrier detection */
		CarrierDetectionEnable(pAd, 1);
	} else if (cap->carrier_func == TONE_RADAR_V1 &&
			   pCarrierDetect->Enable)
		ToneRadarProgram(pAd);
}

/*
    ==========================================================================
    Description:
	Reset CS state to NORMAL state.
	Arguments:
	    pAd                    Pointer to our adapter

    Return Value:
	None

    Note:
    ==========================================================================
*/
INT CarrierDetectReset(
	IN PRTMP_ADAPTER pAd)
{
	pAd->CommonCfg.CarrierDetect.CD_State = CD_NORMAL;
	return 0;
}

/*
    ==========================================================================
    Description:
	Criteria in CS is a timing difference threshold for a pair of carrier tones. This function is a ioctl uesed to adjust the
	Criteria. (unit: 16us)

	Arguments:
	    pAd			Pointer to our adapter
	    arg			Pointer to the ioctl argument

    Return Value:
	None

    Note:
    ==========================================================================
*/
INT Set_CarrierCriteria_Proc(RTMP_ADAPTER *pAd, RTMP_STRING *arg)
{
	UINT32 Value;
	Value = os_str_tol(arg, 0, 10);
	pAd->CommonCfg.CarrierDetect.criteria = Value;
	return TRUE;
}

/*
    ==========================================================================
    Description:
	ReCheck in CS is a value indicating how many continuous incoming carrier tones is enough us to announce  that there
	is carrier tone (and hence enter SILENT state). This function is a ioctl uesed to adjust the ReCheck value.

	Arguments:
	    pAd			Pointer to our adapter
	    arg			Pointer to the ioctl argument

    Return Value:
	None

    Note:
    ==========================================================================
*/
INT Set_CarrierReCheck_Proc(RTMP_ADAPTER *pAd, RTMP_STRING *arg)
{
	pAd->CommonCfg.CarrierDetect.recheck1 = os_str_tol(arg, 0, 10);
	MTWF_LOG(DBG_CAT_HW, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("Set Recheck = %u\n", pAd->CommonCfg.CarrierDetect.recheck1));
	return TRUE;
}

/*
    ==========================================================================
    Description:
	CarrierGoneThreshold is used to determine whether we should leave SILENT state. When the number of carrier
	tones in a certain period of time is less than CarrierGoneThreshold, we should return to NORMAL state. This function
	is a ioctl uesed to adjust the CarrierGoneThreshold.

	Arguments:
	    pAd			Pointer to our adapter
	    arg			Pointer to the ioctl argument

    Return Value:
	None

    Note:
    ==========================================================================
*/
INT Set_CarrierGoneThreshold_Proc(RTMP_ADAPTER *pAd, RTMP_STRING *arg)
{
	pAd->CommonCfg.CarrierDetect.CarrierGoneThreshold = os_str_tol(arg, 0, 10);
	MTWF_LOG(DBG_CAT_HW, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("Set CarrierGoneThreshold = %u\n", pAd->CommonCfg.CarrierDetect.CarrierGoneThreshold));
	return TRUE;
}

/*
    ==========================================================================
    Description:
	Setting up the carrier debug level. set 0 means to turning off the carrier debug

	Arguments:
	    pAd			Pointer to our adapter
	    arg			Pointer to the ioctl argument

    Return Value:
	None

    Note:
    ==========================================================================
*/
INT	Set_CarrierDebug_Proc(RTMP_ADAPTER *pAd, RTMP_STRING *arg)
{
	pAd->CommonCfg.CarrierDetect.Debug = os_str_tol(arg, 0, 10);
	printk("pAd->CommonCfg.CarrierDetect.Debug = %ld\n", pAd->CommonCfg.CarrierDetect.Debug);
	return TRUE;
}

/*
    ==========================================================================
    Description:
	Delta control the delay line characteristic of the cross correlation energy calculation.
	This function is a ioctl uesed to adjust the Delta value.

	Arguments:
	    pAd			Pointer to our adapter
	    arg			Pointer to the ioctl argument

    Return Value:
	None

    Note:
    ==========================================================================
*/
INT	Set_CarrierDelta_Proc(RTMP_ADAPTER *pAd, RTMP_STRING *arg)
{
	pAd->CommonCfg.CarrierDetect.delta = os_str_tol(arg, 0, 10);
	printk("Delta = %d\n", pAd->CommonCfg.CarrierDetect.delta);
	CarrierDetectionStart(pAd);
	return TRUE;
}

/*
	==========================================================================
	Description:
	To set ON/OFF of the "Not Divide Flag"

	Arguments:
		pAd		Pointer to our adapter
		arg		Pointer to the ioctl argument

	Return Value:
		None

	Note:
	==========================================================================
*/
INT	Set_CarrierDivFlag_Proc(RTMP_ADAPTER *pAd, RTMP_STRING *arg)
{
	pAd->CommonCfg.CarrierDetect.div_flag = os_str_tol(arg, 0, 10);
	printk("DivFlag = %d\n", pAd->CommonCfg.CarrierDetect.div_flag);
	CarrierDetectionStart(pAd);
	return TRUE;
}

/*
    ==========================================================================
    Description:
	Carrier Threshold is the energy threshold for h/w to determine a carrier tone or not.
	This function is a ioctl uesed to adjust the Threshold value.

	Arguments:
	    pAd			Pointer to our adapter
	    arg			Pointer to the ioctl argument

    Return Value:
	None

    Note:
    ==========================================================================
*/
INT	Set_CarrierThrd_Proc(RTMP_ADAPTER *pAd, RTMP_STRING *arg)
{
	pAd->CommonCfg.CarrierDetect.threshold = os_str_tol(arg, 0, 10);
	printk("CarrThrd = %d(0x%x)\n", pAd->CommonCfg.CarrierDetect.threshold, pAd->CommonCfg.CarrierDetect.threshold);
	CarrierDetectionStart(pAd);
	return TRUE;
}

/*
	==========================================================================
	Description:
	Carrier SymRund is the number of round bits in Radar Symmetric Round Bits Option.
	This function is a ioctl uesed to adjust the SymRund. (unit: bit)

	Arguments:
		pAd		Pointer to our adapter
		arg		Pointer to the ioctl argument

	Return Value:
		None

	Note:
	==========================================================================
*/
INT	Set_CarrierSymRund_Proc(RTMP_ADAPTER *pAd, RTMP_STRING *arg)
{
	pAd->CommonCfg.CarrierDetect.SymRund = os_str_tol(arg, 0, 10);
	printk("SymRund = %d\n", pAd->CommonCfg.CarrierDetect.SymRund);
	CarrierDetectionStart(pAd);
	return TRUE;
}

/*
	==========================================================================
	Description:
	Carrier Masks are used to prevent false trigger while doing Rx_PE, Packet_End, and AGC tuning.
	This function is a ioctl uesed to adjust these three mask. (unit: 100ns)

	Arguments:
		pAd		Pointer to our adapter
		arg		Pointer to the ioctl argument

	Return Value:
		None

	Note:
	==========================================================================
*/
INT Set_CarrierMask_Proc(RTMP_ADAPTER *pAd, RTMP_STRING *arg)
{
	pAd->CommonCfg.CarrierDetect.VGA_Mask = os_str_tol(arg, 0, 10);
	pAd->CommonCfg.CarrierDetect.Packet_End_Mask = os_str_tol(arg, 0, 10);
	pAd->CommonCfg.CarrierDetect.Rx_PE_Mask = os_str_tol(arg, 0, 10);
	printk("CarrMask = %u(%x)\n", pAd->CommonCfg.CarrierDetect.VGA_Mask, pAd->CommonCfg.CarrierDetect.VGA_Mask);
	CarrierDetectionStart(pAd);
	return TRUE;
}

/*
    ==========================================================================
    Description:
	Initialize CS parameters.

	Arguments:
	    pAd			Pointer to our adapter

    Return Value:
	None

    Note:
    ==========================================================================
*/
VOID CSInit(
	IN PRTMP_ADAPTER pAd)
{
	PCARRIER_DETECTION_STRUCT pCarrierDetect = &pAd->CommonCfg.CarrierDetect;
	pCarrierDetect->TimeStamp = 0;
	pCarrierDetect->recheck = pCarrierDetect->recheck1;
	pCarrierDetect->OneSecIntCount = 0;
	pCarrierDetect->bCsInit = FALSE;
}

/*
    ==========================================================================
    Description:
	To trigger CS start

	Arguments:
	    pAd			Pointer to our adapter

    Return Value:
	None

    Note:
    ==========================================================================
*/
VOID CarrierDetectionStart(PRTMP_ADAPTER pAd)
{
	/*ULONG Value;*/
	/* Enable Bandwidth usage monitor*/
	MTWF_LOG(DBG_CAT_HW, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("CarrierDetectionStart\n"));
	/*RTMP_IO_READ32(pAd->hdev_ctrl, CH_TIME_CFG, &Value);*/
	/*RTMP_IO_WRITE32(pAd->hdev_ctrl, CH_TIME_CFG, Value | 0x1f);	*/

	/* Init Carrier Detect*/
	if (pAd->CommonCfg.CarrierDetect.Enable) {
		CSInit(pAd);
		ToneRadarProgram(pAd);

		/* trun on interrupt polling for pcie device */
		if (pAd->infType == RTMP_DEV_INF_PCIE)
			AsicSendCommandToMcu(pAd, CD_INT_POLLING_CMD, 0xff, 0x01, 0x00, FALSE);

		pAd->CommonCfg.CarrierDetect.bCsInit = TRUE;
	}
}

/*
    ==========================================================================
    Description:
	To stop CS

	Arguments:
	    pAd			Pointer to our adapter

    Return Value:
	None

    Note:
    ==========================================================================
*/
VOID CarrierDetectionStop(IN PRTMP_ADAPTER	pAd)
{
	CarrierDetectReset(pAd);
	CarrierDetectionEnable(pAd, 0);
	return;
}

/*
    ==========================================================================
    Description:
	To program CS related BBP registers (CS initialization)

	Arguments:
	    pAd			Pointer to our adapter

    Return Value:
	None

    Note:
    ==========================================================================
*/
static VOID ToneRadarProgram(PRTMP_ADAPTER pAd)
{
	ULONG threshold;
	struct wifi_dev *wdev = get_default_wdev(pAd);
	CHAR bw = HcGetBw(pAd, wdev);

	/* if wireless mode is 20Mhz mode, then the threshold should div by 2 */
	if (bw  == BW_20)
		threshold = pAd->CommonCfg.CarrierDetect.threshold >> 1;
	else
		threshold = pAd->CommonCfg.CarrierDetect.threshold;

	/* Call ToneRadarProgram_v1/ToneRadarProgram_v2*/
	RTMP_CHIP_CARRIER_PROGRAM(pAd, threshold);
}

/*
    ==========================================================================
    Description:
	To program CS v1 related BBP registers (CS initialization)

	Arguments:
	    pAd			Pointer to our adapter

    Return Value:
	None

    Note:
    ==========================================================================
*/
VOID ToneRadarProgram_v1(PRTMP_ADAPTER pAd, ULONG threshold)
{
	UCHAR bbp;
	MTWF_LOG(DBG_CAT_HW, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("ToneRadarProgram v1\n"));
	/* programe delta delay & division bit*/
	BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R184, 0xf0);
	bbp = pAd->CommonCfg.CarrierDetect.delta << 4;
	bbp |= (pAd->CommonCfg.CarrierDetect.div_flag & 0x1) << 3;
	BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R185, bbp);
	/* program threshold*/
	BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R184, 0x34);
	BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R185, (threshold & 0xff000000) >> 24);
	BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R184, 0x24);
	BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R185, (threshold & 0xff0000) >> 16);
	BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R184, 0x14);
	BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R185, (threshold & 0xff00) >> 8);
	BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R184, 0x04);
	BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R185, threshold & 0xff);
	/* ToneRadarEnable v1 */
	BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R184, 0x05);
}

/*
    ==========================================================================
    Description:
	To program CS v2 related BBP registers (CS initialization)

	Arguments:
	    pAd			Pointer to our adapter

    Return Value:
	None

    Note:
    ==========================================================================
*/
VOID ToneRadarProgram_v2(PRTMP_ADAPTER pAd, ULONG threshold)
{
	UCHAR bbp;
	/* programe delta delay & division bit*/
	MTWF_LOG(DBG_CAT_HW, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("ToneRadarProgram v2\n"));
	bbp = pAd->CommonCfg.CarrierDetect.delta |							\
		  ((pAd->CommonCfg.CarrierDetect.SymRund & 0x3) << 4)	 |		\
		  ((pAd->CommonCfg.CarrierDetect.div_flag & 0x1) << 6) |		\
		  0x80;	/* Full 40MHz Detection Mode */
	RTMP_CARRIER_IO_WRITE8(pAd, 5, bbp);
	/* program *_mask*/
	RTMP_CARRIER_IO_WRITE8(pAd, 2, pAd->CommonCfg.CarrierDetect.VGA_Mask);
	RTMP_CARRIER_IO_WRITE8(pAd, 3, pAd->CommonCfg.CarrierDetect.Packet_End_Mask);
	RTMP_CARRIER_IO_WRITE8(pAd, 4, pAd->CommonCfg.CarrierDetect.Rx_PE_Mask);
	/* program threshold*/
	RTMP_CARRIER_IO_WRITE8(pAd, 6, threshold & 0xff);
	RTMP_CARRIER_IO_WRITE8(pAd, 7, (threshold & 0xff00) >> 8);
	RTMP_CARRIER_IO_WRITE8(pAd, 8, (threshold & 0xff0000) >> 16);
	RTMP_CARRIER_IO_WRITE8(pAd, 9, (threshold & 0xff000000) >> 24);
	/* ToneRadarEnable v2 */
	CarrierDetectionEnable(pAd, 1);
}

/*
    ==========================================================================
    Description:
	To program CS v3 related BBP registers (CS initialization)

	Arguments:
	    pAd			Pointer to our adapter

    Return Value:
	None

    Note:
    ==========================================================================
*/
VOID ToneRadarProgram_v3(PRTMP_ADAPTER pAd, ULONG threshold)
{
	/*
		Carrier Sense (Tone Radar) BBP initialization
		(MT7650 Carrier sense programming guide_v1_20120824.docx)
	*/
	MTWF_LOG(DBG_CAT_HW, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("ToneRadarProgram v3\n"));
	CarrierDetectionEnable(pAd, 0);
	RTMP_BBP_IO_WRITE32(pAd, TR_R2, 0x002d002d);
	RTMP_BBP_IO_WRITE32(pAd, TR_R3, 0x0003002d);
	RTMP_BBP_IO_WRITE32(pAd, TR_R5, 0x80000000);
	RTMP_BBP_IO_WRITE32(pAd, TR_R6, 0x80100000);
	CarrierDetectionEnable(pAd, 1);
}


#endif /* CARRIER_DETECTION_SUPPORT */

