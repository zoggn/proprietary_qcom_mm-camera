/*============================================================================

  Copyright (c) 2013 Qualcomm Technologies, Inc. All Rights Reserved.
  Qualcomm Technologies Proprietary and Confidential.

============================================================================*/
#include <unistd.h>
#include "camera_dbg.h"
#include "colorcorrect32.h"
#include "isp_log.h"

#ifdef ENABLE_CC_LOGGING
  #undef ISP_DBG
#define ISP_DBG ALOGE
#endif

#undef CDBG_ERROR
#define CDBG_ERROR ALOGE

#define MAX_CC_GAIN 3.9

#define GET_CC_MATRIX(CC, M) ({ \
  M[0][0] = CC->c0; \
  M[0][1] = CC->c1; \
  M[0][2] = CC->c2; \
  M[1][0] = CC->c3; \
  M[1][1] = CC->c4; \
  M[1][2] = CC->c5; \
  M[2][0] = CC->c6; \
  M[2][1] = CC->c7; \
  M[2][2] = CC->c8; })

  /* Chromatix stores the coeffs in RGB order whereas              *
   * VFE stores the coeffs in GBR order. Hence c0 maps to M[1][1]  */
#define SET_ISP_CC_MATRIX(CC, M, q) ({ \
  CC->C0 = M[1][1]; \
  CC->C1 = M[1][2]; \
  CC->C2 = M[1][0]; \
  CC->C3 = M[2][1]; \
  CC->C4 = M[2][2]; \
  CC->C5 = M[2][0]; \
  CC->C6 = M[0][1]; \
  CC->C7 = M[0][2]; \
  CC->C8 = M[0][0]; })

#define CC_APPLY_GAIN(cc, gain) ({ \
  cc->c0 *= gain; \
  cc->c1 *= gain; \
  cc->c2 *= gain; \
  cc->c3 *= gain; \
  cc->c4 *= gain; \
  cc->c5 *= gain; \
  cc->c6 *= gain; \
  cc->c7 *= gain; \
  cc->c8 *= gain; \
  cc->k0 *= gain; \
  cc->k1 *= gain; \
  cc->k2 *= gain; \
})

  /* Chromatix stores the coeffs in RGB order whereas             *
   * VFE stores the coeffs in GBR order. Hence c0 maps to M[1][1] */
#define SET_CC_FROM_AWB_MATRIX(CC, M) ({ \
  CC->C0 = FLOAT_TO_Q(7, M[1][1]); \
  CC->C1 = FLOAT_TO_Q(7, M[1][2]); \
  CC->C2 = FLOAT_TO_Q(7, M[1][0]); \
  CC->C3 = FLOAT_TO_Q(7, M[2][1]); \
  CC->C4 = FLOAT_TO_Q(7, M[2][2]); \
  CC->C5 = FLOAT_TO_Q(7, M[2][0]); \
  CC->C6 = FLOAT_TO_Q(7, M[0][1]); \
  CC->C7 = FLOAT_TO_Q(7, M[0][2]); \
  CC->C8 = FLOAT_TO_Q(7, M[0][0]); })

/** util_color_correct_convert_table:
 *    @pInCC: in table
 *    @pOutCC:  out table
 *
 * Convert color correct table.
 *
 * This function executes in ISP thread context
 *
 * Return none.
 **/
static void util_color_correct_convert_table(
  chromatix_color_correction_type *pInCC, color_correct_type* pOutCC)
{
  pOutCC->c0 = CC_COEFF(pInCC->c0, pInCC->q_factor+7);
  pOutCC->c1 = CC_COEFF(pInCC->c1, pInCC->q_factor+7);
  pOutCC->c2 = CC_COEFF(pInCC->c2, pInCC->q_factor+7);
  pOutCC->c3 = CC_COEFF(pInCC->c3, pInCC->q_factor+7);
  pOutCC->c4 = CC_COEFF(pInCC->c4, pInCC->q_factor+7);
  pOutCC->c5 = CC_COEFF(pInCC->c5, pInCC->q_factor+7);
  pOutCC->c6 = CC_COEFF(pInCC->c6, pInCC->q_factor+7);
  pOutCC->c7 = CC_COEFF(pInCC->c7, pInCC->q_factor+7);
  pOutCC->c8 = CC_COEFF(pInCC->c8, pInCC->q_factor+7);
  pOutCC->k0 = pInCC->k0;
  pOutCC->k1 = pInCC->k1;
  pOutCC->k2 = pInCC->k2;
  pOutCC->q_factor = pInCC->q_factor + 7;
}

/** util_color_correct_convert_table_all:
 *    @mce: pointer to instance private data
 *    @in_params: input data
 *
 * Convert all color correct table.
 *
 * This function executes in ISP thread context
 *
 * Return 0 on success.
 **/
static void util_color_correct_convert_table_all(isp_color_correct_mod_t *mod,
  isp_hw_pix_setting_params_t *in_params)
{
  chromatix_parms_type *chromatix_ptrs =
    in_params->chromatix_ptrs.chromatixPtr;
  chromatix_CC_type *chromatix_CC = &chromatix_ptrs->chromatix_VFE.chromatix_CC;

  util_color_correct_convert_table(
    &chromatix_CC->STROBE_color_correction,
    &mod->table.chromatix_STROBE_color_correction);
  util_color_correct_convert_table(
    &chromatix_CC->TL84_color_correction,
    &mod->table.chromatix_TL84_color_correction);
  util_color_correct_convert_table(
    &chromatix_CC->lowlight_color_correction,
    &mod->table.chromatix_yhi_ylo_color_correction);
  util_color_correct_convert_table(
    &chromatix_CC->outdoor_color_correction,
    &mod->table.chromatix_outdoor_color_correction);
  util_color_correct_convert_table(
    &chromatix_CC->LED_color_correction,
    &mod->table.chromatix_LED_color_correction_VF);
  util_color_correct_convert_table(
    &chromatix_CC->D65_color_correction,
    &mod->table.chromatix_D65_color_correction_VF);
  util_color_correct_convert_table(
    &chromatix_CC->A_color_correction,
    &mod->table.chromatix_A_color_correction_VF);
} /*util_color_correct_convert_table_all*/

/** util_color_correct_debug:
 *    @p_cmd: Pointer to color correct configuration.
 *
 * Print color correct configuration.
 *
 * This function executes in ISP thread context
 *
 * Return none.
 **/
static void util_color_correct_debug(ISP_ColorCorrectionCfgCmdType* p_cmd)
{
  ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: coefQFactor = %d\n", __func__, p_cmd->coefQFactor);

  ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: C[0-8] = %d, %d, %d, %d, %d, %d, %d, %d, %d\n", __func__,
    p_cmd->C0, p_cmd->C1, p_cmd->C2, p_cmd->C3, p_cmd->C4,
    p_cmd->C5, p_cmd->C6, p_cmd->C7, p_cmd->C8);

  ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: K[0-2] = %d, %d, %d\n", __func__,
    p_cmd->K0, p_cmd->K1, p_cmd->K2);

} /* util_color_correct_debug*/

/** util_color_correct_interpolate:
 *    @in1: in data 1
 *    @in2: in data 2
 *    @out: result
 *    @ratio: ratio
 *
 * Color correct interpolate.
 *
 * This function executes in ISP thread context
 *
 * Return none.
 **/
static void util_color_correct_interpolate(color_correct_type* in1,
  color_correct_type* in2, color_correct_type* out, float ratio)
{
  out->c0 = LINEAR_INTERPOLATION(in1->c0, in2->c0, ratio);
  out->c1 = LINEAR_INTERPOLATION(in1->c1, in2->c1, ratio);
  out->c2 = LINEAR_INTERPOLATION(in1->c2, in2->c2, ratio);
  out->c3 = LINEAR_INTERPOLATION(in1->c3, in2->c3, ratio);
  out->c4 = LINEAR_INTERPOLATION(in1->c4, in2->c4, ratio);
  out->c5 = LINEAR_INTERPOLATION(in1->c5, in2->c5, ratio);
  out->c6 = LINEAR_INTERPOLATION(in1->c6, in2->c6, ratio);
  out->c7 = LINEAR_INTERPOLATION(in1->c7, in2->c7, ratio);
  out->c8 = LINEAR_INTERPOLATION(in1->c8, in2->c8, ratio);

  out->k0 = LINEAR_INTERPOLATION(in1->k0, in2->k0, ratio);
  out->k1 = LINEAR_INTERPOLATION(in1->k1, in2->k1, ratio);
  out->k2 = LINEAR_INTERPOLATION(in1->k2, in2->k2, ratio);
  out->q_factor = in1->q_factor;
} /*util_color_correct_interpolate*/

/** util_color_correct_interpolate:
 *    @p_cmd: It will be apply in registers
 *    @effects_matrix: effects matrix
 *    @p_cc: color correct
 *    @dig_gain: digital gain
 *
 * Set color correction params.
 *
 * This function executes in ISP thread context
 *
 * Return none.
 **/
static void util_set_color_correction_params(
  ISP_ColorCorrectionCfgCmdType* p_cmd, float effects_matrix[3][3],
  color_correct_type* p_cc, float dig_gain)
{
  int i, j;
  float coeff[3][3], out_coeff[3][3];

#ifdef ENABLE_CC_LOGGING
  PRINT_2D_MATRIX(3, 3, effects_matrix);
#endif
  if (IS_UNITY_MATRIX(effects_matrix, 3)) {
    ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: No effects enabled", __func__);
    GET_CC_MATRIX(p_cc, out_coeff);
  } else {
    ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: Effects enabled", __func__);
    GET_CC_MATRIX(p_cc, coeff);
    MATRIX_MULT(effects_matrix, coeff, out_coeff, 3, 3, 3);
  }
#ifdef ENABLE_CC_LOGGING
  PRINT_2D_MATRIX(3, 3, out_coeff);
#endif
  SET_ISP_CC_MATRIX(p_cmd, out_coeff, (p_cc->q_factor));
  ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: dig_gain %5.3f", __func__, dig_gain);

  p_cmd->C2 = (int32_t)(128 * dig_gain) - (p_cmd->C0 + p_cmd->C1);
  p_cmd->C5 = (int32_t)(128 * dig_gain) - (p_cmd->C3 + p_cmd->C4);
  p_cmd->C6 = (int32_t)(128 * dig_gain) - (p_cmd->C7 + p_cmd->C8);

  p_cmd->K0 = p_cc->k1;
  p_cmd->K1 = p_cc->k2;
  p_cmd->K2 = p_cc->k0;

  p_cmd->coefQFactor = p_cc->q_factor - 7;
  return;
} /*util_set_color_correction_params*/

/** util_color_correct_populate_matrix:
 *    @m: effect matrix
 *    @s: in effect.
 *
 * Populate matrix.
 *
 * This function executes in ISP thread context
 *
 * Return none.
 **/
static void util_color_correct_populate_matrix(float m[3][3], float s)
{
  m[0][0] = 0.2990 + 1.4075 * 0.498 * s;
  m[0][1] = 0.5870 - 1.4075 * 0.417 * s;
  m[0][2] = 0.1140 - 1.4075 * 0.081 * s;
  m[1][0] = 0.2990 + 0.3455 * 0.168 * s - 0.7169 * 0.498 * s;
  m[1][1] = 0.5870 + 0.3455 * 0.330 * s + 0.7169 * 0.417 * s;
  m[1][2] = 0.1140 - 0.3455 * 0.498 * s + 0.7169 * 0.081 * s;
  m[2][0] = 0.2990 - 1.7790 * 0.168 * s;
  m[2][1] = 0.5870 - 1.7790 * 0.330 * s;
  m[2][2] = 0.1140 + 1.7790 * 0.498 * s;
} /*util_color_correct_populate_matrix*/

/** util_color_correct_calc_flash_trigger:
 *    @mod: pointer to instance private data
 *    @tblCCT: In color correct table
 *    @tblOut: Out color correct table
 *    @in_params: in params
 *
 * Calculate flash params.
 *
 * This function executes in ISP thread context
 *
 * Return none.
 **/
static void util_color_correct_calc_flash_trigger(isp_color_correct_mod_t *mod,
  color_correct_type *tblCCT, color_correct_type *tblOut,
  isp_pix_trigger_update_input_t *in_params)
{
  float ratio;
  float p_ratio;
  float flash_start, flash_end;
  color_correct_type *tblFlash = NULL;
  isp_flash_params_t *flash_params = &(in_params->cfg.flash_params);
  chromatix_VFE_common_type *chrComPtr =
    (chromatix_VFE_common_type *)in_params->cfg.chromatix_ptrs.chromatixPtr;
  chromatix_rolloff_type *chromatix_rolloff = &chrComPtr->chromatix_rolloff;
  cam_flash_mode_t *flash_mode = &(in_params->trigger_input.flash_mode);
  chromatix_parms_type *chromatix_ptrs = in_params->cfg.chromatix_ptrs.chromatixPtr;
  chromatix_CC_type *chromatix_CC = &chromatix_ptrs->chromatix_VFE.chromatix_CC;

  if ((int)flash_params->flash_type == CAMERA_FLASH_STROBE) {
    tblFlash = &(mod->table.chromatix_STROBE_color_correction);
    flash_start = chromatix_CC->CC_LED_start;
    flash_end = chromatix_CC->CC_LED_end;
  } else {
    tblFlash = &(mod->table.chromatix_LED_color_correction_VF);
    flash_start = chromatix_CC->CC_LED_start;
    flash_end = chromatix_CC->CC_LED_end;
  }

  if (*flash_mode == CAM_FLASH_MODE_TORCH) {
    if (flash_params->sensitivity_led_low != 0)
      ratio = flash_params->sensitivity_led_off / flash_params->sensitivity_led_low;
    else
      ratio = flash_end;
  } else if (*flash_mode == CAM_FLASH_MODE_ON) {
    if (flash_params->sensitivity_led_hi != 0)
      ratio = flash_params->sensitivity_led_off / flash_params->sensitivity_led_hi;
    else
      ratio = flash_end;
  } else //assume flash off. To be changed when AUTO mode is added
    ratio = flash_start;

  ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: flash_start %5.2f flash_end %5.2f \n", __func__, flash_start,
    flash_end);

  if (ratio >= flash_end)
    *tblOut = *tblFlash;
  else if (ratio <= flash_start) {
    *tblOut = *tblCCT;
  } else {
    p_ratio = GET_INTERPOLATION_RATIO(ratio, flash_start, flash_end);
    util_color_correct_interpolate(tblCCT, tblFlash, tblOut, p_ratio);
  }
  return;
} /* util_color_correct_calc_flash_trigger */

/** util_color_correct_calc_aec_trigger:
 *    @mod: pointer to instance private data
 *    @tblCCT: In color correct table
 *    @tblOut: Out color correct table
 *    @in_params: in params
 *
 * Calculate aec params.
 *
 * This function executes in ISP thread context
 *
 * Return none.
 **/
static int util_color_correct_calc_aec_trigger(
  isp_color_correct_mod_t *mod, color_correct_type *tblCCT,
  color_correct_type *tblOut, isp_pix_trigger_update_input_t *in_params)
{
  int rc = 0;
  chromatix_parms_type *chromatix_ptr =
    in_params->cfg.chromatix_ptrs.chromatixPtr;
  chromatix_CC_type *chromatix_CC = &chromatix_ptr->chromatix_VFE.chromatix_CC;
  trigger_point_type  *lowlight_trigger_point = NULL;
  trigger_point_type *outdoor_trigger_point = NULL;
  trigger_ratio_t aec_ratio_type;
  color_correct_type tbl_lowlight, tbl_outdoor;
  int is_burst = IS_BURST_STREAMING(&(in_params->cfg));

  tbl_outdoor = mod->table.chromatix_outdoor_color_correction;
  tbl_lowlight = mod->table.chromatix_yhi_ylo_color_correction;

  outdoor_trigger_point =
    &(chromatix_ptr->chromatix_VFE.chromatix_gamma.gamma_outdoor_trigger);
  lowlight_trigger_point = &(chromatix_CC->cc_trigger);

  rc = isp_util_get_aec_ratio2(mod->notify_ops->parent,
    chromatix_CC->control_cc,
    outdoor_trigger_point, lowlight_trigger_point,
    &(in_params->trigger_input.stats_update.aec_update),
    is_burst, &aec_ratio_type);

  /* aec_ratio_type.ratio is the ratio to tbl_CCT */
  switch (aec_ratio_type.lighting) {
  case TRIGGER_LOWLIGHT:
    /* interpolate between normal CCT tbl and lowlight tbl */
    util_color_correct_interpolate(tblCCT,
      &tbl_lowlight, tblOut, aec_ratio_type.ratio);
    break;
  case TRIGGER_OUTDOOR:
    /* interpolate between normal CCT tbl and lowlight tbl */
    util_color_correct_interpolate(tblCCT,
      &tbl_outdoor, tblOut, aec_ratio_type.ratio);
    break;
  case TRIGGER_NORMAL:
    *tblOut = *tblCCT;
    break;
  default:
    ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: invalid lighting type, lighting type %d\n",
      __func__, aec_ratio_type.lighting);
    break;
  }

  return rc;
}

/** util_color_correct_calc_awb_trigger:
 *    @mod: pointer to instance private data
 *    @cc_data: In color correct table
 *    @is_snapmode: is snapshot mode
 *    @in_params: in params
 *
 * Calculate awb params.
 *
 * This function executes in ISP thread context
 *
 * Return none.
 **/
static void util_color_correct_calc_awb_trigger(isp_color_correct_mod_t* mod,
  color_correct_type* tbl_out, isp_pix_trigger_update_input_t* in_params)
{
  chromatix_parms_type *chroma_ptr =
    (chromatix_parms_type *)in_params->cfg.chromatix_ptrs.chromatixPtr;
  chromatix_CC_type *chromatix_CC = &chroma_ptr->chromatix_VFE.chromatix_CC;
  cct_trigger_info trigger_info;
  float ratio = 0.0;

  trigger_info.mired_color_temp =
    MIRED(in_params->trigger_input.stats_update.awb_update.color_temp);

  CALC_CCT_TRIGGER_MIRED(trigger_info.trigger_A,
    chromatix_CC->CC_A_trigger);
  CALC_CCT_TRIGGER_MIRED(trigger_info.trigger_d65,
    chromatix_CC->CC_Daylight_trigger);

  awb_cct_type cct_type = isp_util_get_awb_cct_type(
    mod->notify_ops->parent, &trigger_info, in_params);

  ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: cct type %d", __func__, cct_type);
  switch (cct_type) {
  case AWB_CCT_TYPE_A:
    *tbl_out = mod->table.chromatix_A_color_correction_VF;
    break;
  case AWB_CCT_TYPE_TL84_A:
    ratio = GET_INTERPOLATION_RATIO(trigger_info.mired_color_temp,
        trigger_info.trigger_A.mired_start, trigger_info.trigger_A.mired_end);
      ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: AWB_CCT_TYPE_TL84_A ratio %f", __func__, ratio);
      util_color_correct_interpolate(
        &mod->table.chromatix_TL84_color_correction,
        &mod->table.chromatix_A_color_correction_VF,
        tbl_out, ratio);
    break;
  case AWB_CCT_TYPE_D65_TL84:
    ratio = GET_INTERPOLATION_RATIO(trigger_info.mired_color_temp,
        trigger_info.trigger_d65.mired_end,
        trigger_info.trigger_d65.mired_start);
      ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: AWB_CCT_TYPE_D65_TL84 ratio %f", __func__, ratio);
    util_color_correct_interpolate(
      &mod->table.chromatix_D65_color_correction_VF,
      &mod->table.chromatix_TL84_color_correction, tbl_out, ratio);
    break;
  case AWB_CCT_TYPE_D65:
    *tbl_out = mod->table.chromatix_D65_color_correction_VF;
    break;
  default:
  case AWB_CCT_TYPE_TL84:
    *tbl_out = mod->table.chromatix_TL84_color_correction;
    break;
  }
} /*util_color_correct_calc_awb_trigger*/

/** color_correct_set_effect:
 *    @mod: pointer to instance private data
 *    @in_params: input data
 *    @in_params_size: size of input data
 *
 * Set effect.
 *
 * This function executes in ISP thread context
 *
 * Return 0 on success.
 **/
static int color_correct_set_effect(isp_color_correct_mod_t *mod,
  isp_hw_pix_setting_params_t *in_params, uint32_t in_param_size)
{
  int type;
  float s;
  if (in_param_size != sizeof(isp_hw_pix_setting_params_t)) {
    /* size mismatch */
    CDBG_ERROR("%s: size mismatch, expecting = %d, received = %d",
      __func__, sizeof(isp_hw_pix_setting_params_t), in_param_size);
    return -1;
  }

  if (!mod->enable) {
    ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: CC not enabled", __func__);
    return 0;
  }
  type = in_params->effects.effect_type_mask;
  SET_UNITY_MATRIX(mod->effects_matrix, 3);
  if (type & (1 << ISP_EFFECT_SATURATION)) {
    s = 2.0 * in_params->effects.saturation;
    if (F_EQUAL(in_params->effects.hue, 0))
      util_color_correct_populate_matrix(mod->effects_matrix, s);
  }
  if (type & (1 << ISP_EFFECT_HUE)) {
    s = 2.0 * in_params->effects.hue;
    if (!F_EQUAL(in_params->effects.saturation, .5))
      util_color_correct_populate_matrix(mod->effects_matrix, s);
  }
  mod->hw_update_pending = TRUE;
  return 0;
} /*color_correct_set_effect*/

/** color_correct_set_bestshot:
 *    @mod: pointer to instance private data
 *    @in_params: input data
 *    @in_params_size: size of input data
 *
 * Set scene.
 *
 * This function executes in ISP thread context
 *
 * Return 0 on success.
 **/
static int color_correct_set_bestshot(isp_color_correct_mod_t *mod,
  isp_hw_pix_setting_params_t *in_params, uint32_t in_param_size)
{
  cam_scene_mode_type mode = in_params->bestshot_mode;

  mod->skip_trigger = FALSE;

  if (in_param_size != sizeof(isp_hw_pix_setting_params_t)) {
    /* size mismatch */
    CDBG_ERROR("%s: size mismatch, expecting = %d, received = %d",
      __func__, sizeof(isp_hw_pix_setting_params_t), in_param_size);
    return -1;
  }

  ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: mode %d", __func__, mode);
  switch (mode) {
  case CAM_SCENE_MODE_NIGHT:
    mod->final_table = mod->table.chromatix_yhi_ylo_color_correction;
    break;
  default:
    mod->final_table = mod->table.chromatix_TL84_color_correction;
    break;
  }
  mod->hw_update_pending = TRUE;
  return 0;
} /* color_correct_set_bestshot */

/** color_correct_init:
 *    @mod_ctrl: pointer to instance private data
 *    @in_params: input data
 *    @notify_ops: notify
 *
 * Open and initialize all required submodules
 *
 * This function executes in ISP thread context
 *
 * Return 0 on success.
 **/
static int color_correct_init(void *mod_ctrl, void *in_params,
  isp_notify_ops_t *notify_ops)
{
  isp_color_correct_mod_t *mod = mod_ctrl;
  isp_hw_mod_init_params_t *init_params = in_params;

  mod->fd = init_params->fd;
  mod->notify_ops = notify_ops;
  mod->prev_flash_mode = CAM_FLASH_MODE_OFF;
  mod->old_streaming_mode = CAM_STREAMING_MODE_MAX;
  return 0;
} /* color_correct_init */

/** color_correct_config:
 *    @mod: pointer to instance private data
 *    @pix_setting: input data
 *    @in_param_size: size of input data
 *
 * Configure module.
 *
 * This function executes in ISP thread context
 *
 * Return 0 on success.
 **/
static int color_correct_config(isp_color_correct_mod_t *mod,
  isp_hw_pix_setting_params_t *pix_setting, uint32_t in_param_size)
{
  int rc = 0;
  chromatix_parms_type *chromatix_ptr =
    (chromatix_parms_type *)pix_setting->chromatix_ptrs.chromatixPtr;

  if (in_param_size != sizeof(isp_hw_pix_setting_params_t)) {
    /* size mismatch */
    CDBG_ERROR("%s: size mismatch, expecting = %d, received = %d",
      __func__, sizeof(isp_hw_pix_setting_params_t), in_param_size);
    return -1;
  }

  if (!mod->enable) {
    CDBG_HIGH("%s: Mod not Enable.", __func__);
    return rc;
  }
  /* initial param */
  //no effect during config
  SET_UNITY_MATRIX(mod->effects_matrix, 3);
  mod->dig_gain = 1.0;
  mod->trigger_enable = TRUE;

  util_color_correct_convert_table_all(mod, pix_setting);

  //default table
  mod->final_table = mod->table.chromatix_TL84_color_correction;

  util_set_color_correction_params(&mod->RegCmd, mod->effects_matrix,
    &(mod->final_table), mod->dig_gain);

  mod->skip_trigger = FALSE;
  mod->hw_update_pending = TRUE;

  return rc;
} /* color_correct_config */

/** color_correct_destroy:
 *    @mod_ctrl: pointer to instance private data
 *
 * Destroy all open submodule and and free private resources.
 *
 * This function executes in ISP thread context
 *
 * Return 0 on success.
 **/
static int color_correct_destroy(void *mod_ctrl)
{
  isp_color_correct_mod_t *mod = mod_ctrl;

  memset(mod, 0, sizeof(isp_color_correct_mod_t));
  free(mod);
  return 0;
} /* color_correct_destroy */

/** color_correct_enable:
 *    @mod: pointer to instance private data
 *    @enable: input data
 *    @in_params_size: size of input data
 *
 * Enable module.
 *
 * This function executes in ISP thread context
 *
 * Return 0 on success.
 **/
static int color_correct_enable(isp_color_correct_mod_t *mod,
  isp_mod_set_enable_t *enable, uint32_t in_param_size)
{
  if (in_param_size != sizeof(isp_mod_set_enable_t)) {
    /* size mismatch */
    CDBG_ERROR("%s: size mismatch, expecting = %d, received = %d",
      __func__, sizeof(isp_mod_set_enable_t), in_param_size);
    return -1;
  }

  mod->enable = enable->enable;
  if (!mod->enable)
    mod->hw_update_pending = 0;
  return 0;
} /* color_correct_enable */

/** color_correct_trigger_enable:
 *    @mod: pointer to instance private data
 *    @enable: input data
 *    @in_params_size: size of input data
 *
 * Trigger enable.
 *
 * This function executes in ISP thread context
 *
 * Return 0 on success.
 **/
static int color_correct_trigger_enable(isp_color_correct_mod_t *mod,
  isp_mod_set_enable_t *enable, uint32_t in_param_size)
{
  if (in_param_size != sizeof(isp_mod_set_enable_t)) {
    CDBG_ERROR("%s: size mismatch, expecting = %d, received = %d",
      __func__, sizeof(isp_mod_set_enable_t), in_param_size);
    return -1;
  }
  mod->trigger_enable = enable->enable;
  return 0;
} /* color_correct_trigger_enable */

/** color_correct_trigger_update:
 *    @mod: pointer to instance private data
 *    @in_params: input data
 *    @in_params_size: size of input data
 *
 * Update configuration.
 *
 * This function executes in ISP thread context
 *
 * Return 0 on success.
 **/
static int color_correct_trigger_update(isp_color_correct_mod_t *mod,
  isp_pix_trigger_update_input_t *in_params, uint32_t in_param_size)
{
  int rc = 0;

  chromatix_parms_type *chromatix_ptr =
    in_params->cfg.chromatix_ptrs.chromatixPtr;
  chromatix_CC_type *chromatix_CC = &chromatix_ptr->chromatix_VFE.chromatix_CC;
  int update_cc;
  cam_flash_mode_t flash_mode = in_params->trigger_input.flash_mode;
  color_correct_type tblCCT;
  float (*p_effects_matrix)[3] = NULL, effects_matrix[3][3];
  int is_burst = IS_BURST_STREAMING(&(in_params->cfg));
  ISP_ColorCorrectionCfgCmdType* p_cmd = &mod->RegCmd;
  awb_update_t *awb_update = &(in_params->trigger_input.stats_update.awb_update);

  if (in_param_size != sizeof(isp_pix_trigger_update_input_t)) {
    CDBG_ERROR("%s: size mismatch, expecting = %d, received = %d\n",
      __func__, sizeof(isp_pix_trigger_update_input_t), in_param_size);
    return -1;
  }

  if (!mod->enable || !mod->trigger_enable || mod->skip_trigger) {
    ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: no trigger update, enable %d, trigger_enb %d, skip trigger %d\n",
      __func__, mod->enable, mod->trigger_enable, mod->skip_trigger);
    return rc;
  }

  if (in_params->trigger_input.stats_update.awb_update.color_temp == 0) {
    ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: zero color temperature\n", __func__);
    return rc;
  }

  if (!isp_util_aec_check_settled(&in_params->trigger_input.stats_update.aec_update)
      && !is_burst && !in_params->trigger_input.is_init_setting) {
    ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: AEC not settled\n", __func__);
    return rc;
  }
  in_params->trigger_input.is_init_setting = FALSE;

  /* If Bestshot enabled, use all 1 effect matrix*/
  if (in_params->cfg.bestshot_mode != CAM_SCENE_MODE_OFF) {
    SET_UNITY_MATRIX(effects_matrix, 3);
    p_effects_matrix = effects_matrix;
  } else {
    p_effects_matrix = mod->effects_matrix;
  }

  update_cc =
    (mod->old_streaming_mode != in_params->cfg.streaming_mode) ||
    (mod->color_temp !=
      in_params->trigger_input.stats_update.awb_update.color_temp) ||
      (mod->prev_flash_mode != in_params->trigger_input.flash_mode);

  if (!update_cc) {
     ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: no update CC, update_cc = %d\n", __func__, update_cc);
    mod->hw_update_pending = FALSE;
    return 0;
  }

  /* If the ccm flag is set, use the ccm matrix inside AWB Update and    *
   *  skip all interpolation including awb, aec, effects and write to hw */
  if (awb_update->ccm_flag != 0) {
    SET_CC_FROM_AWB_MATRIX(p_cmd, awb_update->cur_ccm);
    ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: dig_gain %5.3f", __func__, mod->dig_gain);

    p_cmd->C2 = (int32_t)(128 * mod->dig_gain) - ( p_cmd->C0 + p_cmd->C1);
    p_cmd->C5 = (int32_t)(128 * mod->dig_gain) - ( p_cmd->C3 + p_cmd->C4);
    p_cmd->C6 = (int32_t)(128 * mod->dig_gain) - ( p_cmd->C7 + p_cmd->C8);

    p_cmd->K0 = mod->final_table.k0;
    p_cmd->K1 = mod->final_table.k1;
    p_cmd->K2 = mod->final_table.k2;

    p_cmd->coefQFactor = mod->final_table.q_factor-7;
    mod->prev_flash_mode = in_params->trigger_input.flash_mode;
    mod->hw_update_pending = TRUE;
    return 0;
  }

  /* Do AWB's CCT Interpolate: interpolated tables are used regardeless Flash
   *       is on or not. So derive them before checking Flash on or not. */
  util_color_correct_calc_awb_trigger(mod, &tblCCT, in_params);

  /* Do AEC trigger: Flash Interpolate.
     Lowlight, Outdoor(use gamma outdoor trigger as system workaround).*/
  if ((in_params->cfg.flash_params.flash_type != CAMERA_FLASH_NONE) &&
      (flash_mode != CAM_FLASH_MODE_OFF)) {
    util_color_correct_calc_flash_trigger(mod, &tblCCT, &mod->final_table, in_params);
  } else {
    rc = util_color_correct_calc_aec_trigger(mod, &tblCCT, &mod->final_table, in_params);
    if (rc < 0) {
      CDBG_ERROR("%s: failed calculate aec trigger, rc = %d\n", __func__, rc);
      return rc;
    }
  }

  util_set_color_correction_params(p_cmd,
    p_effects_matrix, &(mod->final_table), mod->dig_gain);

  mod->hw_update_pending = TRUE;
  return 0;
} /* color_correct_trigger_update */

/** color_correct_set_chromatix:
 *    @mod: pointer to instance private data
 *    @in_params: input data
 *    @in_params_size: size of input data
 *
 * Set chromatix.
 *
 * This function executes in ISP thread context
 *
 * Return 0 on success.
 **/
static int color_correct_set_chromatix(isp_color_correct_mod_t *mod,
  isp_hw_pix_setting_params_t *in_params, uint32_t in_param_size)
{
  if (in_param_size != sizeof(isp_hw_pix_setting_params_t)) {
    /* size mismatch */
    CDBG_ERROR("%s: size mismatch, expecting = %d, received = %d",
      __func__, sizeof(isp_hw_pix_setting_params_t), in_param_size);
    return -1;
  }

  util_color_correct_convert_table_all(mod, in_params);

  mod->final_table = mod->table.chromatix_TL84_color_correction;

  mod->hw_update_pending = TRUE;
  return 0;
}

/** color_correct_do_hw_update:
 *    @color_correc_mod: pointer to instance private data
 *
 * Update hardware configuration.
 *
 * This function executes in ISP thread context
 *
 * Return 0 on success.
 **/
static int color_correct_do_hw_update(isp_color_correct_mod_t *color_corr_mod)
{
  int rc = 0;
  struct msm_vfe_cfg_cmd2 cfg_cmd;
  struct msm_vfe_reg_cfg_cmd reg_cfg_cmd[1];

  if (color_corr_mod->hw_update_pending) {
    cfg_cmd.cfg_data = (void *)&color_corr_mod->RegCmd;
    cfg_cmd.cmd_len = sizeof(color_corr_mod->RegCmd);
    cfg_cmd.cfg_cmd = (void *)reg_cfg_cmd;
    cfg_cmd.num_cfg = 1;

    reg_cfg_cmd[0].u.rw_info.cmd_data_offset = 0;
    reg_cfg_cmd[0].cmd_type = VFE_WRITE;
    reg_cfg_cmd[0].u.rw_info.reg_offset = ISP_COLOR_COR32_OFF;
    reg_cfg_cmd[0].u.rw_info.len = ISP_COLOR_COR32_LEN * sizeof(uint32_t);

    util_color_correct_debug(&color_corr_mod->RegCmd);
    rc = ioctl(color_corr_mod->fd, VIDIOC_MSM_VFE_REG_CFG, &cfg_cmd);
    if (rc < 0) {
      CDBG_ERROR("%s: HW update error, rc = %d", __func__, rc);
      return rc;
    }
    color_corr_mod->applied_RegCmd = color_corr_mod->RegCmd;
    color_corr_mod->hw_update_pending = 0;
  }
  /* TODO: update hw reg */
  return rc;
} /* color_correct_hw_reg_update */

/** color_correct_set_params:
 *    @mod_ctrl: pointer to instance private data
 *    @params_id: parameter ID
 *    @in_params: input data
 *    @in_params_size: size of input data
 *
 * Set parameter function. It handle all input parameters.
 *
 * This function executes in ISP thread context
 *
 * Return 0 on success.
 **/
static int color_correct_set_params(void *mod_ctrl, uint32_t param_id,
  void *in_params, uint32_t in_param_size)
{
  isp_color_correct_mod_t *mod = mod_ctrl;
  int rc = 0;

  switch (param_id) {
  case ISP_HW_MOD_SET_CHROMATIX_RELOAD:
    rc = color_correct_set_chromatix(mod,
      (isp_hw_pix_setting_params_t *)in_params, in_param_size);
    break;
  case ISP_HW_MOD_SET_MOD_ENABLE:
    rc = color_correct_enable(mod, (isp_mod_set_enable_t *)in_params,
      in_param_size);
    break;
  case ISP_HW_MOD_SET_MOD_CONFIG:
    rc = color_correct_config(mod, (isp_hw_pix_setting_params_t *)in_params,
      in_param_size);
    break;
  case ISP_HW_MOD_SET_TRIGGER_ENABLE:
    rc = color_correct_trigger_enable(mod, (isp_mod_set_enable_t *)in_params,
      in_param_size);
    break;
  case ISP_HW_MOD_SET_TRIGGER_UPDATE:
    rc = color_correct_trigger_update(mod,
      (isp_pix_trigger_update_input_t *)in_params, in_param_size);
    break;
  case ISP_HW_MOD_SET_BESTSHOT:
    rc = color_correct_set_bestshot(mod,
      (isp_hw_pix_setting_params_t *)in_params, in_param_size);
    break;
  case ISP_HW_MOD_SET_EFFECT:
    rc = color_correct_set_effect(mod, (isp_hw_pix_setting_params_t *)in_params,
      in_param_size);
    break;
  default:
    CDBG_ERROR("%s: param_id %d, is not supported in this module\n",
      __func__, param_id);
    break;
  }
  return rc;
} /* color_correct_set_params */

/** sce_ez_vfe_update
 *
 * description: update vfe params
 *
 **/
static void colorcorr_ez_vfe_update(colorcorrection_t *colorcorr,
  ISP_ColorCorrectionCfgCmdType *colorcorrCfg)
{
    colorcorr->coef_qfactor = colorcorrCfg->coefQFactor;
    colorcorr->coef_rtor    = colorcorrCfg->C0;
    colorcorr->coef_gtor    = colorcorrCfg->C1;
    colorcorr->coef_btor    = colorcorrCfg->C2;
    colorcorr->coef_rtog    = colorcorrCfg->C3;
    colorcorr->coef_gtog    = colorcorrCfg->C4;
    colorcorr->coef_btog    = colorcorrCfg->C5;
    colorcorr->coef_rtob    = colorcorrCfg->C6;
    colorcorr->coef_gtob    = colorcorrCfg->C7;
    colorcorr->coef_btob    = colorcorrCfg->C8;
    colorcorr->roffset      = colorcorrCfg->K0;
    colorcorr->boffset      = colorcorrCfg->K1;
    colorcorr->goffset      = colorcorrCfg->K2;
}

/** color_correct_get_params:
 *    @mod_ctrl: pointer to instance private data
 *    @params_id: parameter ID
 *    @in_params: input data
 *    @in_params_size: size of input data
 *    @out_params: output data
 *    @out_params_size: size of output data
 *
 * Get parameter function. It handle all parameters.
 *
 * This function executes in ISP thread context
 *
 * Return 0 on success.
 **/
static int color_correct_get_params(void *mod_ctrl, uint32_t param_id,
  void *in_params, uint32_t in_param_size, void *out_params,
  uint32_t out_param_size)
{
  isp_color_correct_mod_t *mod = mod_ctrl;
  int rc = 0;

  switch (param_id) {
  case ISP_HW_MOD_GET_MOD_ENABLE: {
    isp_mod_get_enable_t *enable = out_params;

    if (sizeof(isp_mod_get_enable_t) != out_param_size) {
      CDBG_ERROR("%s: error, out_param_size mismatch, param_id = %d",
        __func__, param_id);
      break;
    }
    enable->enable = mod->enable;
    break;
  }

  case ISP_HW_MOD_GET_VFE_DIAG_INFO_USER: {
    vfe_diagnostics_t *vfe_diag = (vfe_diagnostics_t *)out_params;
    colorcorrection_t *colorcorr = &vfe_diag->prev_colorcorr;
    if (sizeof(vfe_diagnostics_t) != out_param_size) {
      CDBG_ERROR("%s: error, out_param_size mismatch, param_id = %d",
        __func__, param_id);
      break;
    }
    /*Populate vfe_diag data for example*/
    ISP_DBG(ISP_MOD_COLOR_CORRECT, "%s: Populating vfe_diag data", __func__);
    if (NULL == colorcorr || NULL == mod ) {
      CDBG_ERROR("%s: NULL colorcorr %x mod %x", __func__,
        (unsigned int)colorcorr, (unsigned int)mod);
      break;
    }
    vfe_diag->control_colorcorr.enable = mod->enable;
    vfe_diag->control_colorcorr.cntrlenable = mod->trigger_enable;
    colorcorr_ez_vfe_update(colorcorr, &mod->applied_RegCmd);
  }
    break;
  default:
    rc = -1;
    break;
  }
  return rc;
} /* color_correct_get_params */

/** color_correct_action:
 *    @mod_ctrl: pointer to instance private data
 *    @action_code: action id
 *    @action_data: action data
 *    @action_data_size: action data size
 *
 * Handle all actions.
 *
 * This function executes in ISP thread context
 *
 * Return 0 on success.
 **/
static int color_correct_action(void *mod_ctrl, uint32_t action_code,
  void *data, uint32_t data_size)
{
  int rc = 0;
  isp_color_correct_mod_t *mod = mod_ctrl;

  switch (action_code) {
  case ISP_HW_MOD_ACTION_HW_UPDATE:
    rc = color_correct_do_hw_update(mod);
    break;
  default:
    /* no op */
    CDBG_HIGH("%s: action code = %d is not supported. nop",
      __func__, action_code);
    rc = -EAGAIN;
    break;
  }
  return rc;
} /* color_correct_action */

/** color_correct32_open:
 *    @version: version of isp
 *
 * Allocate instance private data for module.
 *
 * This function executes in ISP thread context
 *
 * Return pointer to struct which contain module operations.
 **/
isp_ops_t *color_correct32_open(uint32_t version)
{
  isp_color_correct_mod_t *mod = malloc(sizeof(isp_color_correct_mod_t));

  if (!mod) {
    CDBG_ERROR("%s: fail to allocate memory", __func__);
    return NULL;
  }
  memset(mod, 0, sizeof(isp_color_correct_mod_t));

  mod->ops.ctrl = (void *)mod;
  mod->ops.init = color_correct_init;
  mod->ops.destroy = color_correct_destroy;
  mod->ops.set_params = color_correct_set_params;
  mod->ops.get_params = color_correct_get_params;
  mod->ops.action = color_correct_action;

  return &mod->ops;
} /* color_correct32_open */

