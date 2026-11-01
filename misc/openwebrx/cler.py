# Copyright (C) 2026 CaribouLabs.
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Derivative of OpenWebRX (AGPL-3.0). Install as owrx/source/cler.py in an
# OpenWebRX checkout; it is not part of the cler binary distribution.
from owrx.source.connector import ConnectorSource, ConnectorDeviceDescription
from owrx.form.input import Input, TextInput
from owrx.form.input.device import GainInput
from typing import List


class ClerSource(ConnectorSource):
    def getCommandMapper(self):
        return super().getCommandMapper().setBase("openwebrx_connector")


class ClerDeviceDescription(ConnectorDeviceDescription):
    def getName(self):
        return "cler (HackRF, PlutoSDR, USRP, CaribouLite, SoapySDR, SigMF, simulator)"

    def getInputs(self) -> List[Input]:
        return super().getInputs() + [
            TextInput(
                "device",
                "Device",
                infotext="cler source id, e.g. hackrf, hackrf:0000000000000000a06063c8, "
                "pluto:ip:192.168.2.1, uhd:serial=317, cariboulite:s1g, soapy:driver=rtlsdr, "
                "sigmf:capture_name, sim. Empty picks the first device found.",
            ),
            GainInput("rf_gain", "Gain", has_agc=True),
        ]

    def getDeviceOptionalKeys(self):
        return super().getDeviceOptionalKeys() + ["device"]

    def getProfileOptionalKeys(self):
        return super().getProfileOptionalKeys()
