/*
 * Copyright (c) 2013-2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <stdlib.h>
#include "config-parser.h"

#define GST_CAT_DEFAULT gst_nvm_player_debug
GST_DEBUG_CATEGORY (gst_nvm_player_debug);

static GstNvmResult
_parser_get_parameter_index (
    GstNvmConfigParamsMap *params_map,
    gchar *param_name,
    guint *index)
{
    gint i = 0;

    while (params_map[i].param_name != NULL) {
        if (strcasecmp (params_map[i].param_name, param_name) == 0) {
            *index = i;
            return GST_NVM_RESULT_OK;
        } else {
            i++;
        }
    }

    return GST_NVM_RESULT_INVALID_ARGUMENT;
}

static GstNvmResult
_config_parser_get_file_content (
    gchar *file_name,
    gchar **file_content)
{
    FILE *file;
    glong file_size;
    gchar *file_content_temp;

    file = fopen (file_name, "r");
    if (file == NULL) {
        GST_ERROR ("Cannot open configuration file %s", file_name);
        return GST_NVM_RESULT_FILE_NOT_FOUND;
    }

    if (fseek (file, 0, SEEK_END) != 0) {
        GST_ERROR ("Cannot fseek in configuration file %s", file_name);
        return GST_NVM_RESULT_FAIL;
    }

    file_size = ftell (file);
    if (file_size < 0 || file_size > 150000) {
        GST_ERROR ("Unreasonable size %ld encountered for config file %s",
                   file_size, file_name);
        return GST_NVM_RESULT_FAIL;
    }

    if (fseek (file, 0, SEEK_SET) != 0) {
        GST_ERROR ("Cannot fseek in configuration file %s", file_name);
        return GST_NVM_RESULT_FAIL;
    }

    file_content_temp = g_malloc (file_size + 1);
    if (file_content_temp == NULL) {
        GST_ERROR ("Failed allocating buffer for file content");
        return GST_NVM_RESULT_OUT_OF_MEMORY;
    }

    file_size = (glong) fread (file_content_temp, 1, file_size, file);
    file_content_temp[file_size] = '\0';
    *file_content = file_content_temp;

    fclose(file);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_config_parser_parse_file (
    GstNvmConfigParamsMap *params_map,
    gint size_of_section,
    gchar *file_name,
    gint *param_sets)
{
    gchar *config_content = NULL;
    gchar *items[GST_NVM_MAX_ITEMS_TO_PARSE + 1] = {NULL};
    gint items_count = 0, i = 0, curr_section_index = 0;
    gint int_value;
    guint uint_value;
    gdouble double_value;
    guint cur_item_index, param_default_length;
    gboolean is_in_string = 0, is_in_item = 0;
    gchar *buff, *buff_end, *param_location;
    GstNvmResult result;

    result = _config_parser_get_file_content (file_name, &config_content);
    if (result != GST_NVM_RESULT_OK) {
        GST_ERROR ("_config_parser_get_file_content failed");
        return result;
    }

    buff = config_content;
    buff_end = &config_content[strlen (config_content)];

    // Step one: Create mapping in the content using "items" pointers array.
    // For each parameter parse 3 items: param name, '='  and the param value.
    while (buff < buff_end) {
        if (items_count >= GST_NVM_MAX_ITEMS_TO_PARSE) {
            GST_WARNING ("Number of items in configuration file exceeded the maximum allowed (%d). Only %d items will be parsed",
                        GST_NVM_MAX_ITEMS_TO_PARSE, GST_NVM_MAX_ITEMS_TO_PARSE);
            items_count = GST_NVM_MAX_ITEMS_TO_PARSE;
            break;
        }
        switch (*buff) {
            case 13:
                ++buff;
                break;
            case '#':
                *buff = '\0'; // Replace '#' with '\0'
                while (*buff != '\n' && buff < buff_end) // Skip till EOL or EOF
                    ++buff;
                is_in_string = 0;
                is_in_item = 0;
                break;
            case '\n':
                is_in_item = 0;
                is_in_string = 0;
                *buff++ = '\0';
                break;
            case ' ':
            case '\t':                                   // Skip whitespace
                if (is_in_string)
                    buff++;
                else { // Terminate non-strings once whitespace is found
                    *buff++ = '\0';
                    is_in_item = 0;
                }
                break;
            case '"':                                    // Begin/End of String
                *buff++ = '\0';
                if (!is_in_string) {
                    items[items_count++] = buff;
                    is_in_item = ~is_in_item;
                } else {
                    is_in_item = 0;
                }
                is_in_string = ~is_in_string;            // Toggle
                break;
            case '[':
                *(buff++) = '\0';
                items[items_count++] = buff;
                // Skip till whitespace or EOL or EOF
                while (*buff != ' ' && *buff != '\n' && buff < buff_end) {
                    i++; buff++;
                }

                i = 0;
                while (*buff == ' ')
                    *(buff++) = '\0';
                items[items_count++] = buff;
                // Read the section number
                while (*buff != ']' && *buff != '\n' && buff < buff_end) {
                    buff++;
                }
                *(buff++) = '\0';
                is_in_string = 0;
                is_in_item = 0;
                break;
            default:
                if (!is_in_item) {
                    items[items_count++] = buff;
                    is_in_item = ~is_in_item;
                }
                buff++;
        }
    }

    items_count--;

    // Step 2: Go through the mapping and save their values in parameters map
    for(i = 0; i < items_count; i += 3) {
        if (!strcmp (items[i], "capture-params-set")) {
            curr_section_index = atoi(items[i + 1]);
            if (curr_section_index > GST_NVM_MAX_CONFIG_SECTIONS) {
                 GST_WARNING ("Max number of capture configuration sections reached (%d). Stopping config file parsing at section %d\n",
                              GST_NVM_MAX_CONFIG_SECTIONS, curr_section_index);
                 break;
            }
            curr_section_index--;
            i -= 1;
            continue;
        }

        if (_parser_get_parameter_index (params_map,
                                         items[i],
                                         &cur_item_index) != GST_NVM_RESULT_OK) {
            GST_WARNING ("Parameter Name '%s' is not recognized. Dismissing this parameter.", items[i]);
            continue;
        }

        if (strcmp ("=", items[i + 1])) {
            GST_ERROR ("Parsing error: '=' expected as the second token in each line. Error caught while parsing parameter '%s'.",
                items[i]);
            i -= 2;
            continue;
        }

        param_location = (char *) params_map[cur_item_index].mapped_location +
                                  curr_section_index * size_of_section;
        param_default_length = params_map[cur_item_index].string_length;

        // Interpret the Value
        GST_DEBUG ("Interpreting parameter %s", items[i]);
        switch (params_map[cur_item_index].type) {
            case GST_NVM_TYPE_INT:
                if (sscanf (items[i + 2], "%d", &int_value) != 1) {
                    GST_ERROR (
                        "Parsing Error: Expected numerical value for parameter %s, found value '%s'",
                        items[i], items[i + 2]);
                }
                *(int *)(void *)param_location = int_value;
                break;
            case GST_NVM_TYPE_UINT:
                if (sscanf (items[i + 2], "%u", &uint_value) != 1) {
                    GST_ERROR ("Parsing Error: Expected numerical value for parameter %s, found value '%s'", items[i], items[i + 2]);
                }
                *(guint *)(void *)param_location = uint_value;
                break;
            case GST_NVM_TYPE_UINT_HEX:
                if (sscanf (items[i + 2], "%x", &uint_value) != 1) {
                    GST_ERROR ("Parsing Error: Expected unsigned char value for parameter %s, found value '%s'",
                                items[i], items[i + 2]);
                }
                *(guint *)(void *)param_location = uint_value;
                break;
            case GST_NVM_TYPE_CHAR_ARR:
                if (items[i + 2] == NULL)
                    memset (param_location, 0, param_default_length);
                else {
                    strncpy (param_location, items[i + 2], param_default_length);
                    param_location[param_default_length - 1] = '\0';
                }
                break;
            case GST_NVM_TYPE_DOUBLE:
                if (sscanf (items[i + 2], "%lf", &double_value) != 1) {
                    GST_ERROR ("Parsing Error: Expected double value for parameter %s found value '%s'",
                                items[i], items[i + 2]);
                }
                *(double *)(void *)param_location = double_value;
                break;
            case GST_NVM_TYPE_UCHAR_ARR:
                if (items[i + 2] == NULL)
                    memset (param_location, 0, param_default_length);
                else {
                    strncpy (param_location, items[i + 2], param_default_length);
                    param_location[param_default_length - 1] = '\0';
                }
                break;
           default:
                GST_ERROR ("Encountered unknown value type in the map: %d",
                           params_map[cur_item_index].type);
        }
    }

    *param_sets = curr_section_index + 1;
    g_free(config_content);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_config_parser_init_params_map (
    GstNvmConfigParamsMap *params_map)
{
    gint i = 0;

    while (params_map[i].param_name != NULL) {
        if (params_map[i].mapped_location == NULL) {
           i++;
           continue;
        }
        switch (params_map[i].type) {
            case GST_NVM_TYPE_UINT:
            case GST_NVM_TYPE_UINT_HEX:
                *(guint *) (params_map[i].mapped_location) =
                                        (guint) params_map[i].default_value;
                break;
            case GST_NVM_TYPE_INT:
                *(gint *) (params_map[i].mapped_location) =
                                        (gint) params_map[i].default_value;
                break;
            case GST_NVM_TYPE_DOUBLE:
                *(gdouble *) (params_map[i].mapped_location) =
                                        (gdouble) params_map[i].default_value;
                break;
            case GST_NVM_TYPE_UCHAR:
                *(gboolean *) (params_map[i].mapped_location) =
                                        (gboolean) params_map[i].default_value;
                break;
            case GST_NVM_TYPE_USHORT:
                *(gushort *) (params_map[i].mapped_location) =
                                        (gushort) params_map[i].default_value;
                break;
            case GST_NVM_TYPE_ULLONG:
                *(gulong *) (params_map[i].mapped_location) =
                                        (gulong) params_map[i].default_value;
                break;
            case GST_NVM_TYPE_CHAR_ARR:
            case GST_NVM_TYPE_UCHAR_ARR:
                break;
            default:
                GST_ERROR ("Encountered unknown type parameter: %d",
                           params_map[i].type);
                break;
        }
        i++;
    }

    return GST_NVM_RESULT_OK;
}
