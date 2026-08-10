import { mkdir, readFile, writeFile } from "node:fs/promises"
import React from "react"
import { Circuit } from "@tscircuit/core"
import { convertCircuitJsonToPcbSvg, convertCircuitJsonToSchematicSvg } from "circuit-to-svg"
import Board from "./index.circuit"

const circuit = new Circuit()
circuit.add(<Board />)
await circuit.renderUntilSettled()
const circuitJson = circuit.getCircuitJson()

const errors = circuitJson.filter((item: any) =>
  String(item.type).endsWith("_error") || String(item.type).endsWith("_warning"))
const hardErrors = errors.filter((item: any) => String(item.type).endsWith("_error"))

const outputs: Record<string, string> = {
  "generated/audio-cap-circuit.json": JSON.stringify(circuitJson, null, 2) + "\n",
  "generated/audio-cap-pcb.svg": convertCircuitJsonToPcbSvg(circuitJson as any),
  "generated/audio-cap-schematic.svg": convertCircuitJsonToSchematicSvg(circuitJson as any),
  "generated/audio-cap-checks.json": JSON.stringify({
    status: hardErrors.length ? "errors" : "review",
    hardErrorCount: hardErrors.length,
    diagnosticCount: errors.length,
    diagnostics: errors,
    notice: "Rev A reference placement; unique footprints and routing require first-article review"
  }, null, 2) + "\n",
}

await mkdir("generated", { recursive: true })
if (process.argv.includes("--check")) {
  let stale = false
  for (const [path, content] of Object.entries(outputs)) {
    try {
      if (await readFile(path, "utf8") !== content) stale = true
    } catch { stale = true }
  }
  if (stale) throw new Error("Audio Cap PCB review artifacts are stale; run npm run build")
} else {
  for (const [path, content] of Object.entries(outputs)) await writeFile(path, content)
}

console.log(`Audio Cap PCB: ${circuitJson.length} elements, ${hardErrors.length} hard errors, ${errors.length} diagnostics`)
