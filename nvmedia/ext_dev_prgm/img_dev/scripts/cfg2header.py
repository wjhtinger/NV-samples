# Copyright (c) 2016-2017 NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

import re
import sys
import os
def convert2header (*args):
    #Text for Copywrite
    line0 = "/* Copyright (c) 2016-2017 NVIDIA CORPORATION.  All rights reserved.\n"
    line1 = " *\n"
    line2 = " * NVIDIA CORPORATION and its licensors retain all intellectual property\n"
    line3 = " * and proprietary rights in and to this software, related documentation\n"
    line4 = " * and any modifications thereto.  Any use, reproduction, isclosure or\n"
    line5 = " * distribution of this software and related documentation without an express\n"
    line6 = " * license agreement from NVIDIA CORPORATION is strictly prohibited.*/\n"
    # output file should be last argument
    out_file = open(args[len(args)-1], 'w')
    out_file.writelines([line0, line1, line2, line3, line4, line5, line6])
    out_file.write("// GENERATED FILE - DO NOT MODIFY!\n")
    symname = list()
    for i in range(0, len(args) - 1):
        in_file = open(args[i])
        insize = os.path.getsize(in_file.name) + 1
        base_name = os.path.basename(args[i])
        print base_name
        (file_name, ext) = os.path.splitext(base_name)
        symname.append (str(file_name + "ConfigurationData"))
        out_file.write(" const char "+ symname[i] +"["+ str(insize)+"] =\n")

        for line in in_file:
            newline = re.search("\n", line)
            if newline:
                quotes_escaped = re.sub("\"", "\\\"", re.sub("\n", "", line))
                out_file.write ("\""+quotes_escaped+"\\n\"\n")
            else:
                 quotes_escaped = re.sub("\"", "\\\"", re.sub("\n", "", line))
                 out_file.write ("\""+quotes_escaped+"\"")

        out_file.write(";\n")
        in_file.close()

    out_file.write("const char * cameraConfigTable["+ str(len(args) - 1) +"] = {\n")
    for k in range(0, len(args)-1):
        out_file.write("\t"+symname[k]+",\n")
    out_file.write("};\n")

    out_file.write("const char * cameraConfigString["+ str(len(args) - 1) +"] = {\n")
    for k in range(0, len(args)-1):
        out_file.write("\t\""+symname[k].replace("ConfigurationData", "")+"\",\n")
    out_file.write("};\n")
    out_file.close()

# cfg file name: between _sensor and _pass, no extra underscore '_' allowed
convert2header('../camera_configs/c_ov10640_pass1.cfg',
               '../camera_configs/c_ov10640_pass2.cfg',
               '../camera_configs/c_ov10640svc210_pass1.cfg',
               '../camera_configs/c_ov10640svc210_pass2.cfg',
               '../camera_configs/c_ov10640lsoffsvc212_pass1.cfg',
               '../camera_configs/c_ov10640lsoffsvc212_pass2.cfg',
               '../camera_configs/ref_ar0231_pass1.cfg',
               '../camera_configs/ref_ar0231_pass2.cfg',
               '../camera_configs/ref_ar0231rccb_pass1.cfg',
               '../camera_configs/ref_ar0231rccb_pass2.cfg',
               '../camera_configs/ref_ar0231rccbss3322_pass1.cfg',
               '../camera_configs/ref_ar0231rccbss3322_pass2.cfg',
               '../camera_configs/ref_ar0231rccbss3323_pass1.cfg',
               '../camera_configs/ref_ar0231rccbss3323_pass2.cfg',
               '../camera_configs/ref_ar0231rccbbae_pass1.cfg',
               '../camera_configs/ref_ar0231rccbbae_pass2.cfg',
               '../camera_modules_config.h')
