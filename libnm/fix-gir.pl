#!/usr/bin/perl
# vim: ft=perl ts=2 sts=2 sw=2 et ai
# -*- Mode: perl; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-

# g-ir-scanner < 1.42 does not allow us to correctly annotate GArray-valued or
# GPtrArray-valued properties, so we need this...

%fixups = ( 'NMActiveConnection:devices' => 'NMDevice',
	    'NMClient:active-connections' => 'NMActiveConnection',
	    'NMClient:connections' => 'NMRemoteConnection',
	    'NMClient:devices' => 'NMDevice',
	    'NMDevice:available-connections' => 'NMRemoteConnection',
	    'NMDeviceBond:slaves' => 'NMDevice',
	    'NMDeviceBridge:slaves' => 'NMDevice',
	    'NMDeviceTeam:slaves' => 'NMDevice',
	    'NMDeviceWifi:access-points' => 'NMAccessPoint',
	    'NMDeviceWimax:nsps' => 'NMWimaxNsp',
	    'NMSettingDcb:priority-bandwidth' => 'guint',
	    'NMSettingDcb:priority-flow-control' => 'gboolean',
	    'NMSettingDcb:priority-group-bandwidth' => 'guint',
	    'NMSettingDcb:priority-group-id' => 'guint',
	    'NMSettingDcb:priority-strict-bandwidth' => 'gboolean',
	    'NMSettingDcb:priority-traffic-class' => 'guint',
	    'NMSettingIPConfig:addresses' => 'NMIPAddress',
	    'NMSettingIPConfig:routes' => 'NMIPRoute'
	  );

while (<>) {
  if (/<class name="([^"]*)"/) {
    $current_class = "NM$1";
  } elsif (/<property name="([^"]*)"/) {
    $c_type = $fixups{"$current_class:$1"};
  } elsif (/<\/property>/) {
    $c_type = '';
  }

  if ($c_type && /<type/) {
    if ($c_type =~ /^NM(.*)/) {
      $name = $1;
    } else {
      $name = $c_type;
    }
    s/name="([^"]*)"/name="$name"/;
    s/c:type="([^"]*)"/c:type="$c_type"/;
  }

  print;
}
