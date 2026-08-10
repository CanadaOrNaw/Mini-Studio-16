// Mini Studio Audio Cap Rev A reference layout.
// Status is engraved on the board: NOT HARDWARE VERIFIED.
// Exact production footprints must be checked against the named MPN datasheets
// during first-article review; source coordinates are deliberately explicit.
import React from "react"

const passives = [
  ["C5", "100nF", -7, 4], ["C6", "100nF", -3, 4],
  ["C7", "100nF", 1, 4], ["C8", "100nF", 5, 4],
  ["C11", "10uF", 9, 4], ["C13", "22uF", 13, 4],
] as const

export default () => (
  <board width="80mm" height="20mm" routingDisabled>
    <net name="GND" isGroundNet />
    <net name="V5" />
    <net name="V3V3" />
    <net name="SPI_CLK" />
    <net name="SPI_MOSI" />
    <net name="SPI_MISO" />
    <net name="SPI_CS" />
    <net name="HOST_IRQ" />
    <net name="HOST_RESET" />
    <net name="I2S_BCLK" />
    <net name="I2S_LRCK" />
    <net name="I2S_DATA" />
    <net name="MCLK" />
    <net name="LINE_L" />
    <net name="LINE_R" />
    <net name="USB_DP" />
    <net name="USB_DM" />

    <chip name="J3" manufacturerPartNumber="SSW-107-02-G-D"
      footprint="pinrow14_rows2_cols7_p2.54mm_py2.54mm"
      pcbX={0} pcbY={-6.8} pcbRotation={0}
      pinLabels={{pin1:"V5",pin2:"GND",pin3:"SPI_CS",pin4:"SPI_CLK",
        pin5:"SPI_MISO",pin6:"SPI_MOSI",pin7:"HOST_IRQ",pin8:"HOST_RESET",
        pin9:"NC9",pin10:"NC10",pin11:"NC11",pin12:"NC12",pin13:"NC13",pin14:"GND2"}} />

    <chip name="U1" manufacturerPartNumber="ESP32-WROOM-32E-N4"
      footprint="pinrow38_rows2_cols19_p1.27mm_py18mm_smd"
      pcbX={23} pcbY={0} pcbRotation={90}
      pinLabels={{pin1:"GND",pin2:"V3V3",pin3:"EN",pin4:"GPIO36",pin5:"GPIO39",
        pin6:"GPIO34",pin7:"I2S_DATA",pin8:"PAIR",pin9:"LED",pin10:"I2S_LRCK",
        pin11:"I2S_BCLK",pin12:"GPIO27",pin13:"SPI_CLK",pin14:"SPI_MISO",
        pin15:"GND2",pin16:"SPI_MOSI",pin17:"NC17",pin18:"NC18",pin19:"NC19",
        pin20:"NC20",pin21:"NC21",pin22:"NC22",pin23:"SPI_CS",pin24:"GPIO2",
        pin25:"BOOT",pin26:"HOST_IRQ",pin27:"RX2",pin28:"TX2",pin29:"GPIO5",
        pin30:"GPIO18",pin31:"GPIO19",pin32:"NC32",pin33:"GPIO21",pin34:"RX0",
        pin35:"TX0",pin36:"GPIO22",pin37:"GPIO23",pin38:"GND3"}} />

    <chip name="U2" manufacturerPartNumber="PCM1808PWR" footprint="tssop14_p0.65mm"
      pcbX={-8} pcbY={0} pcbRotation={90}
      pinLabels={{pin1:"VINL",pin2:"VINR",pin3:"VREF",pin4:"AGND",pin5:"V5",
        pin6:"V3V3",pin7:"GND",pin8:"MCLK",pin9:"I2S_LRCK",pin10:"I2S_BCLK",
        pin11:"I2S_DATA",pin12:"MD0",pin13:"MD1",pin14:"FMT"}} />

    <chip name="U3" manufacturerPartNumber="CP2102N-A02-GQFN24"
      footprint="qfn24_w4_h4_p0.5mm_thermalpad" pcbX={9} pcbY={0}
      pinLabels={{pin1:"V3V3",pin2:"GND",pin3:"USB_DP",pin4:"USB_DM",pin5:"TX0",
        pin6:"RX0",pin7:"DTR",pin8:"RTS",pin9:"NC9",pin10:"NC10",pin11:"NC11",
        pin12:"NC12",pin13:"NC13",pin14:"NC14",pin15:"NC15",pin16:"NC16",
        pin17:"NC17",pin18:"NC18",pin19:"NC19",pin20:"NC20",pin21:"NC21",
        pin22:"NC22",pin23:"NC23",pin24:"V5"}} />

    <chip name="U4" manufacturerPartNumber="AP63203WU-7" footprint="sot23_6"
      pcbX={-19} pcbY={2.5}
      pinLabels={{pin1:"SW",pin2:"GND",pin3:"FB",pin4:"EN",pin5:"V5",pin6:"BOOT"}} />
    <chip name="Y1" manufacturerPartNumber="KC3225Z11.2896C16X00"
      footprint="crystal4_w3.2_h2.5" pcbX={-14} pcbY={-3.5}
      pinLabels={{pin1:"ENABLE",pin2:"GND",pin3:"MCLK",pin4:"V3V3"}} />

    <chip name="J1" manufacturerPartNumber="SJ-3523-SMT-TR" footprint="pinrow5_p2mm"
      pcbX={-36} pcbY={0} pcbRotation={90}
      pinLabels={{pin1:"LINE_L",pin2:"LINE_R",pin3:"GND",pin4:"SWL",pin5:"SWR"}} />
    <chip name="J2" manufacturerPartNumber="USB4105-GF-A" footprint="pinrow6_p0.8mm_smd"
      pcbX={36} pcbY={0} pcbRotation={90}
      pinLabels={{pin1:"V5",pin2:"USB_DM",pin3:"USB_DP",pin4:"CC1",pin5:"CC2",pin6:"GND"}} />
    <chip name="SW1" manufacturerPartNumber="KMR221GLFS" footprint="pushbutton4"
      pcbX={30} pcbY={7} pinLabels={{pin1:"PAIR",pin2:"GND",pin3:"PAIR2",pin4:"GND2"}} />
    <led name="LED1" footprint="0603" pcbX={18} pcbY={7} />

    {passives.map(([name, value, x, y]) =>
      <capacitor key={name} name={name} capacitance={value} footprint="0603" pcbX={x} pcbY={y} />)}
    <resistor name="R1" resistance="5.1k" footprint="0603" pcbX={31} pcbY={-4} />
    <resistor name="R2" resistance="5.1k" footprint="0603" pcbX={34} pcbY={-4} />
    <resistor name="R3" resistance="1k" footprint="0603" pcbX={-28} pcbY={3} />
    <resistor name="R4" resistance="1k" footprint="0603" pcbX={-28} pcbY={-3} />
    <capacitor name="C1" capacitance="2.2uF" footprint="0603" pcbX={-23} pcbY={3} />
    <capacitor name="C2" capacitance="2.2uF" footprint="0603" pcbX={-23} pcbY={-3} />

    <trace from=".J3 > .V5" to="net.V5" />
    <trace from=".J3 > .GND" to="net.GND" />
    <trace from=".J3 > .GND2" to="net.GND" />
    <trace from=".J3 > .SPI_CLK" to="net.SPI_CLK" />
    <trace from=".J3 > .SPI_MOSI" to="net.SPI_MOSI" />
    <trace from=".J3 > .SPI_MISO" to="net.SPI_MISO" />
    <trace from=".J3 > .SPI_CS" to="net.SPI_CS" />
    <trace from=".J3 > .HOST_IRQ" to="net.HOST_IRQ" />
    <trace from=".U1 > .SPI_CLK" to="net.SPI_CLK" />
    <trace from=".U1 > .SPI_MOSI" to="net.SPI_MOSI" />
    <trace from=".U1 > .SPI_MISO" to="net.SPI_MISO" />
    <trace from=".U1 > .SPI_CS" to="net.SPI_CS" />
    <trace from=".U1 > .HOST_IRQ" to="net.HOST_IRQ" />
    <trace from=".U1 > .I2S_BCLK" to="net.I2S_BCLK" />
    <trace from=".U1 > .I2S_LRCK" to="net.I2S_LRCK" />
    <trace from=".U1 > .I2S_DATA" to="net.I2S_DATA" />
    <trace from=".U2 > .I2S_BCLK" to="net.I2S_BCLK" />
    <trace from=".U2 > .I2S_LRCK" to="net.I2S_LRCK" />
    <trace from=".U2 > .I2S_DATA" to="net.I2S_DATA" />
    <trace from=".U2 > .MCLK" to="net.MCLK" />
    <trace from=".Y1 > .MCLK" to="net.MCLK" />
    <trace from=".J1 > .LINE_L" to=".R3 > .pin1" />
    <trace from=".R3 > .pin2" to=".C1 > .pin1" />
    <trace from=".C1 > .pin2" to=".U2 > .VINL" />
    <trace from=".J1 > .LINE_R" to=".R4 > .pin1" />
    <trace from=".R4 > .pin2" to=".C2 > .pin1" />
    <trace from=".C2 > .pin2" to=".U2 > .VINR" />
    <trace from=".J2 > .USB_DP" to="net.USB_DP" />
    <trace from=".J2 > .USB_DM" to="net.USB_DM" />
    <trace from=".U3 > .USB_DP" to="net.USB_DP" />
    <trace from=".U3 > .USB_DM" to="net.USB_DM" />

    <pcbnotetext text="MINI STUDIO AUDIO CAP REV A" pcbX={0} pcbY={8.4} fontSize="0.8mm" />
    <pcbnotetext text="NOT HARDWARE VERIFIED" pcbX={0} pcbY={-9} fontSize="0.7mm" />
    <pcbnotetext text="RF KEEP CLEAR" pcbX={30} pcbY={0} fontSize="0.6mm" />
  </board>
)
