# Copyright (c) 2013, NVIDIA Corporation.  All rights reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in  and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.

#!/bin/bash

terminate_script()
{
    popd &>/dev/null
    exit 10
}

check_gst_tools()
{
    gst-inspect-1.0 --version
    rc=$?
    if [[ $rc == 127 ]]; then
        echo 'Error - gst-inspect-1.0 not found'
        echo 'Install GStreamer tools package'
        echo '     sudo apt-get install gstreamer1.0-tools'
        terminate_script
    fi
}

check_gst_base_plugin()
{
    gst-inspect-1.0 alsasink > /dev/null
    rc=$?
    if [[ $rc != 0 ]] ; then
        echo 'Error - Unable to scan for alsasink plugin'
        echo 'Install GStreamer Base and ALSA plugin package'
        echo '    sudo apt-get install gstreamer1.0-plugins-base'
        echo '    sudo apt-get install gstreamer1.0-alsa'
        terminate_script
    fi
}

check_gst_good_plugin()
{
    gst-inspect-1.0 aacparse > /dev/null
    rc=$?
    if [[ $rc != 0 ]] ; then
        echo 'Error - Unable to scan for good plugin'
        echo 'Install GStreamer good plugin package'
        echo '    sudo apt-get install gstreamer1.0-plugins-good'
        terminate_script
    fi
}

check_gst_bad_plugin()
{
    gst-inspect-1.0 mpegtsdemux > /dev/null
    rc=$?
    if [[ $rc != 0 ]] ; then
        echo 'Error - Unable to scan for bad plugin'
        echo 'Install GStreamer bad plugin package'
        echo '    sudo apt-get install gstreamer1.0-plugins-bad'
        terminate_script
    fi
}

check_gst_ugly_plugin()
{
    gst-inspect-1.0 asfdemux > /dev/null
    rc=$?
    if [[ $rc != 0 ]] ; then
        echo 'Error - Unable to scan for ugly plugin'
        echo 'Install GStreamer ugly plugin package'
        echo '    sudo apt-get install gstreamer1.0-plugins-ugly'
        terminate_script
    fi
}

check_nvmedia_video_plugin()
{
    gst-inspect-1.0 nvmediaoverlaysink > /dev/null
    rc=$?
    if [[ $rc != 0 ]] ; then
        echo 'Error - Unable to scan for video plugin (libgstnvmedia-1.0.so)'
        terminate_script
    fi
}

check_nvmedia_audio_plugin()
{
    gst-inspect-1.0 nvmediamp3auddec > /dev/null
    rc=$?
    if [[ $rc != 0 ]] ; then
        echo 'Error - Unable to scan for audio plugin (libgstnvmediaaudio-1.0.so)'
        terminate_script
    fi
}

#start of script

#check for gst-tools
check_gst_tools

#check for base plugin
check_gst_base_plugin

#check for good plugin
check_gst_good_plugin

#check for bad plugin
check_gst_bad_plugin

#check for ugly plugin
check_gst_ugly_plugin

#check for gst-nvmedia video plugin
check_nvmedia_video_plugin

#check for gst-nvmedia audio plugin
check_nvmedia_audio_plugin

echo 'Success - setup has all components needed !!'
exit 0
