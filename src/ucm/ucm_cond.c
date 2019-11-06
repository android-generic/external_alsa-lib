/*
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *  Support for the verb/device/modifier core logic and API,
 *  command line tool and file parser was kindly sponsored by
 *  Texas Instruments Inc.
 *  Support for multiple active modifiers and devices,
 *  transition sequences, multiple client access and user defined use
 *  cases was kindly sponsored by Wolfson Microelectronics PLC.
 *
 *  Copyright (C) 2019 Red Hat Inc.
 *  Authors: Jaroslav Kysela <perex@perex.cz>
 */

#include "ucm_local.h"

static int get_string(snd_config_t *compound, const char *key, const char **str)
{
	snd_config_t *node;
	int err;

	err = snd_config_search(compound, key, &node);
	if (err < 0)
		return err;
	return snd_config_get_string(node, str);
}

static int if_eval_string(snd_use_case_mgr_t *uc_mgr, snd_config_t *eval)
{
	const char *string1 = NULL, *string2 = NULL;
	char *s1, *s2;
	int err;

	err = get_string(eval, "String1", &string1);
	if (err < 0 && err != -ENOENT) {
		uc_error("String error (If.Condition.String1)");
		return -EINVAL;
	}

	err = get_string(eval, "String2", &string2);
	if (err < 0 && err != -ENOENT) {
		uc_error("String error (If.Condition.String2)");
		return -EINVAL;
	}

	if (string1 || string2) {
		if (string1 == NULL) {
			uc_error("If.Condition.String1 not defined");
			return -EINVAL;
		}
		if (string2 == NULL) {
			uc_error("If.Condition.String2 not defined");
			return -EINVAL;
		}
		err = uc_mgr_get_substituted_value(uc_mgr, &s1, string1);
		if (err < 0)
			return err;
		err = uc_mgr_get_substituted_value(uc_mgr, &s2, string2);
		if (err < 0) {
			free(s1);
			return err;
		}
		err = strcasecmp(string1, string2) == 0;
		free(s2);
		free(s1);
		return err;
	}

	err = get_string(eval, "Haystack", &string1);
	if (err < 0 && err != -ENOENT) {
		uc_error("String error (If.Condition.Haystack)");
		return -EINVAL;
	}

	err = get_string(eval, "Needle", &string2);
	if (err < 0 && err != -ENOENT) {
		uc_error("String error (If.Condition.Needle)");
		return -EINVAL;
	}

	if (string1 || string2) {
		if (string1 == NULL) {
			uc_error("If.Condition.Haystack not defined");
			return -EINVAL;
		}
		if (string2 == NULL) {
			uc_error("If.Condition.Needle not defined");
			return -EINVAL;
		}
		err = uc_mgr_get_substituted_value(uc_mgr, &s1, string1);
		if (err < 0)
			return err;
		err = uc_mgr_get_substituted_value(uc_mgr, &s2, string2);
		if (err < 0) {
			free(s1);
			return err;
		}
		err = strstr(string1, string2) == 0;
		free(s2);
		free(s1);
		return err;
	}

	uc_error("Unknown String condition arguments");
	return -EINVAL;
}

static int if_eval_control_exists(snd_use_case_mgr_t *uc_mgr, snd_config_t *eval)
{
	snd_ctl_t *ctl;
	const char *device = NULL, *ctldef;
	snd_ctl_elem_id_t *elem_id;
	snd_ctl_elem_info_t *elem_info;
	char *s;
	int err;


	snd_ctl_elem_id_alloca(&elem_id);
	snd_ctl_elem_info_alloca(&elem_info);

	err = get_string(eval, "Device", &device);
	if (err < 0 && err != -ENOENT) {
		uc_error("ControlExists error (If.Condition.Device)");
		return -EINVAL;
	}

	err = get_string(eval, "Control", &ctldef);
	if (err < 0) {
		uc_error("ControlExists error (If.Condition.Control)");
		return -EINVAL;
	}

	err = uc_mgr_get_substituted_value(uc_mgr, &s, ctldef);
	if (err < 0)
		return err;
	err = snd_ctl_ascii_elem_id_parse(elem_id, s);
	free(s);
	if (err < 0) {
		uc_error("unable to parse element identificator (%s)", ctldef);
		return -EINVAL;
	}

	if (device == NULL) {
		ctl = uc_mgr_get_ctl(uc_mgr);
		if (ctl == NULL) {
			uc_error("cannot determine control device");
			return -EINVAL;
		}
	} else {
		err = uc_mgr_get_substituted_value(uc_mgr, &s, device);
		if (err < 0)
			return err;
		err = uc_mgr_open_ctl(uc_mgr, &ctl, s);
		free(s);
		if (err < 0)
			return err;
	}

	snd_ctl_elem_info_set_id(elem_info, elem_id);
	err = snd_ctl_elem_info(ctl, elem_info);
	if (err < 0)
		return 0;

	return 1;
}

static int if_eval(snd_use_case_mgr_t *uc_mgr, snd_config_t *eval)
{
	const char *type;
	int err;

	if (snd_config_get_type(eval) != SND_CONFIG_TYPE_COMPOUND) {
		uc_error("compound type expected for If.Condition");
		return -EINVAL;
	}

	err = get_string(eval, "Type", &type);
	if (err < 0) {
		uc_error("type block error (If.Condition)");
		return -EINVAL;
	}

	if (strcmp(type, "ControlExists") == 0)
		return if_eval_control_exists(uc_mgr, eval);

	if (strcmp(type, "String") == 0)
		return if_eval_string(uc_mgr, eval);

	uc_error("unknown If.Condition.Type");
	return -EINVAL;
}

static int if_eval_one(snd_use_case_mgr_t *uc_mgr,
		       snd_config_t *cond,
		       snd_config_t **result)
{
	snd_config_t *expr, *_true = NULL, *_false = NULL;
	int err;

	*result = NULL;

	if (snd_config_get_type(cond) != SND_CONFIG_TYPE_COMPOUND) {
		uc_error("compound type expected for If.1");
		return -EINVAL;
	}

	if (snd_config_search(cond, "Condition", &expr) < 0) {
		uc_error("condition block expected (If)");
		return -EINVAL;
	}

	err = snd_config_search(cond, "True", &_true);
	if (err < 0 && err != -ENOENT) {
		uc_error("true block error (If)");
		return -EINVAL;
	}

	err = snd_config_search(cond, "False", &_false);
	if (err < 0 && err != -ENOENT) {
		uc_error("false block error (If)");
		return -EINVAL;
	}

	err = if_eval(uc_mgr, expr);
	if (err > 0) {
		*result = _true;
		return 0;
	} else if (err == 0) {
		*result = _false;
		return 0;
	} else {
		return err;
	}
}

#if 0
static void config_dump(snd_config_t *cfg)
{
	snd_output_t *out;
	snd_output_stdio_attach(&out, stderr, 0);
	snd_output_printf(out, "-----\n");
	snd_config_save(cfg, out);
	snd_output_close(out);
}
#endif

static int compound_merge(snd_config_t *dst, snd_config_t *src)
{
	snd_config_iterator_t i, next;
	snd_config_t *n;
	int err;

	if (snd_config_get_type(src) != SND_CONFIG_TYPE_COMPOUND) {
		uc_error("compound type expected for If True/False block");
		return -EINVAL;
	}

	snd_config_for_each(i, next, src) {
		n = snd_config_iterator_entry(i);
		err = snd_config_remove(n);
		if (err < 0)
			return err;
		err = snd_config_add(dst, n);
		if (err < 0) {
			return err;
		}
	}

	return 0;
}

/*
 * put back the result from all conditions to the parent
 */
int uc_mgr_evaluate_condition(snd_use_case_mgr_t *uc_mgr,
			      snd_config_t *parent,
			      snd_config_t *cond)
{
	snd_config_iterator_t i, i2, next, next2;
	snd_config_t *a, *n, *n2, *parent2;
	const char *id;
	int err;

	if (uc_mgr->conf_format < 2) {
		uc_error("conditions are not supported for v1 syntax");
		return -EINVAL;
	}

	if (snd_config_get_type(cond) != SND_CONFIG_TYPE_COMPOUND) {
		uc_error("compound type expected for If");
		return -EINVAL;
	}

	snd_config_for_each(i, next, cond) {
		n = snd_config_iterator_entry(i);
		err = if_eval_one(uc_mgr, n, &a);
		if (err < 0)
			return err;
		err = snd_config_search(a, "If", &n2);
		if (err < 0 && err != -ENOENT) {
			uc_error("If block error (If)");
			return -EINVAL;
		} else if (err == 0) {
			err = uc_mgr_evaluate_condition(uc_mgr, a, n2);
			if (err < 0)
				return err;
			snd_config_delete(n2);
		}
		snd_config_for_each(i2, next2, a) {
			n2 = snd_config_iterator_entry(i2);
			err = snd_config_remove(n2);
			if (err < 0)
				return err;
			err = snd_config_get_id(n2, &id);
			if (err < 0) {
__add:
				err = snd_config_add(parent, n2);
				if (err < 0)
					return err;
				continue;
			} else {
				err = snd_config_search(parent, id, &parent2);
				if (err == -ENOENT)
					goto __add;
				err = compound_merge(parent2, n2);
				if (err < 0)
					return err;
			}
			snd_config_delete(n2);
		}
	}
	return 0;
}