#!/usr/bin/env bash
# Prints what is capturing right now, one line per kind: "mic <apps>", "cam <apps>", "screen <apps>" (apps joined by ", "; the line is
# left out when nothing uses it). Reads PipeWire's graph: a capture stream that is linked to a microphone, a camera or a screen cast.
# Usage: privacy_scan.sh <out file>   (written atomically)
out="${1:?out file}"
tmp="$out.$$"
pw-dump 2>/dev/null | jq -r '
  [.[] | select(.type == "PipeWire:Interface:Node")] as $nodes
  | [.[] | select(.type == "PipeWire:Interface:Link" and .info.state == "active")] as $links
  | def cls($id): ($nodes[] | select(.id == $id) | .info.props["media.class"]) // "";
    def app($n): ($n.info.props["application.name"] // $n.info.props["node.name"] // "?");
    def linked($n): [$links[] | select(.info["input-node-id"] == $n.id or .info["output-node-id"] == $n.id)
                    | (if .info["input-node-id"] == $n.id then .info["output-node-id"] else .info["input-node-id"] end)];
    [ $nodes[]
      | select((.info.props["stream.capture.sink"] // false) != true and (.info.props["node.name"] // "") != "end4pC")
      | . as $n
      | (.info.props["media.class"] // "") as $c
      | linked($n) as $peers
      | if $c == "Stream/Input/Audio" and ($peers | length) > 0 and ([$peers[] | cls(.)] | any(. == "Audio/Source" or . == "Audio/Source/Virtual" or . == "Audio/Duplex"))
          and ([$peers[] | . as $p | $nodes[] | select(.id == $p) | .info.props["device.class"] // ""] | all(. != "monitor"))
        then {k: "mic", a: app($n)}
        elif $c == "Stream/Input/Video" and ($peers | length) > 0 and ([$peers[] | cls(.)] | any(. == "Video/Source")) then {k: "cam", a: app($n)}
        elif $c == "Stream/Output/Video" and ($peers | length) > 0 then {k: "screen", a: app($n)}
        else empty end ]
    | group_by(.k)[] | "\(.[0].k) \([.[].a] | unique | join(", "))"' > "$tmp" 2>/dev/null
mv -f "$tmp" "$out"
