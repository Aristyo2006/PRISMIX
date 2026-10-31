#include "web_assets.h"

namespace prismix::web {

const std::string& get_web_ui_html() {
    static const std::string s_html = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
  <title>PRISMIX // Industrial Digital Audio Console</title>
  <style>
    :root {
      /* PRISMIX industrial hardware palette */
      --bg: #0D0E0C;
      --chassis: #1A1B17;
      --chassis-elevated: #20211C;
      --panel: #24251F;
      --panel-raised: #2C2D26;
      --control: #30312A;
      --control-hover: #3A3B33;
      --control-active: #1D1E1A;
      --recessed: #090A08;
      --recessed-dark: #040504;
      --modal-backdrop: rgba(5, 6, 5, 0.88);
      --metal-hi: rgba(255, 255, 255, 0.075);
      --metal-hi-soft: rgba(255, 255, 255, 0.035);
      --metal-lo: rgba(0, 0, 0, 0.62);
      --metal-edge: #3A3B34;
      --metal-edge-dark: #11120F;

      /* Fine horizontal brushing. Kept at low contrast and never applied globally. */
      --brushed-grain:
        repeating-linear-gradient(180deg,
          rgba(255, 255, 255, 0.020) 0px,
          rgba(255, 255, 255, 0.020) 1px,
          transparent 1px,
          transparent 3px,
          rgba(0, 0, 0, 0.025) 3px,
          rgba(0, 0, 0, 0.025) 4px,
          transparent 4px,
          transparent 7px);

      /* One reusable material language, differentiated by lighting and tone. */
      --material-chassis:
        radial-gradient(125% 100% at 14% -12%, rgba(255,255,255,0.07), transparent 46%),
        linear-gradient(104deg, transparent 0 31%, rgba(255,255,255,0.024) 45%, transparent 58%),
        radial-gradient(60% 50% at 82% 0%, rgba(154,160,150,0.035), transparent 62%),
        var(--brushed-grain),
        linear-gradient(180deg, #2B2C25 0%, #20211B 50%, #181914 100%);
      --material-panel:
        radial-gradient(110% 90% at 20% -10%, rgba(255,255,255,0.065), transparent 52%),
        linear-gradient(104deg, transparent 0 38%, rgba(255,255,255,0.018) 50%, transparent 62%),
        var(--brushed-grain),
        linear-gradient(180deg, #292A23 0%, #22231D 50%, #1A1B16 100%);
      --material-strip:
        radial-gradient(110% 60% at 50% -12%, rgba(255,255,255,0.075), transparent 62%),
        linear-gradient(112deg, transparent 0 36%, rgba(255,255,255,0.022) 48%, transparent 61%),
        linear-gradient(90deg, rgba(255,255,255,0.015) 0%, transparent 12%, transparent 88%, rgba(0,0,0,0.10) 100%),
        var(--brushed-grain),
        linear-gradient(180deg, #31322B 0%, #282923 48%, #1F201B 100%);
      --material-master:
        radial-gradient(100% 65% at 50% -8%, rgba(224,194,125,0.08), transparent 58%),
        linear-gradient(112deg, transparent 0 35%, rgba(255,255,255,0.018) 48%, transparent 60%),
        linear-gradient(90deg, rgba(255,255,255,0.018) 0%, transparent 16%, transparent 84%, rgba(0,0,0,0.11) 100%),
        var(--brushed-grain),
        linear-gradient(180deg, #37332A 0%, #2C2921 48%, #211F18 100%);
      --material-master-bay:
        radial-gradient(110% 80% at 50% -12%, rgba(210,180,112,0.07), transparent 58%),
        linear-gradient(104deg, transparent 0 40%, rgba(255,255,255,0.014) 50%, transparent 60%),
        var(--brushed-grain),
        linear-gradient(180deg, #2B2821 0%, #211F18 56%, #191713 100%);
      --material-control:
        linear-gradient(180deg, rgba(255,255,255,0.065) 0%, transparent 34%),
        var(--brushed-grain),
        linear-gradient(180deg, #3D3E36 0%, #2E2F28 45%, #22231E 100%);
      --material-control-hover:
        linear-gradient(180deg, rgba(255,255,255,0.085) 0%, transparent 36%),
        var(--brushed-grain),
        linear-gradient(180deg, #474840 0%, #34352D 48%, #272822 100%);
      --material-control-pressed:
        linear-gradient(180deg, #1B1C18 0%, #24251F 100%);
      --material-well:
        radial-gradient(100% 70% at 50% 0%, rgba(255,255,255,0.025), transparent 72%),
        repeating-linear-gradient(180deg, rgba(255,255,255,0.008) 0px, rgba(255,255,255,0.008) 1px, transparent 1px, transparent 5px),
        linear-gradient(180deg, #080907 0%, #0B0C09 46%, #050604 100%);
      --material-readout:
        linear-gradient(180deg, #0B0C09 0%, #070806 100%);

      /* Machined slider channel, cap and drag feedback. */
      --slider-track-fill: rgba(91, 128, 128, 0.48);
      --slider-track-fill-soft: rgba(51, 75, 76, 0.3);
      --material-slider-track:
        linear-gradient(90deg,
          var(--slider-track-fill) 0%,
          var(--slider-track-fill-soft) var(--range-pct, 0%),
          transparent var(--range-pct, 0%),
          transparent 100%),
        linear-gradient(180deg, rgba(255,255,255,0.045) 0%, transparent 38%),
        linear-gradient(180deg, #060705 0%, #0B0C09 48%, #050604 100%);
      --material-slider-thumb:
        linear-gradient(90deg,
          transparent 0 calc(50% - 1px),
          rgba(233, 239, 235, 0.34) calc(50% - 1px),
          rgba(233, 239, 235, 0.34) calc(50% + 1px),
          rgba(0, 0, 0, 0.46) calc(50% + 1px),
          transparent calc(50% + 1px)),
        linear-gradient(180deg, rgba(255,255,255,0.2) 0%, transparent 28%),
        repeating-linear-gradient(180deg, rgba(255,255,255,0.022) 0px, rgba(255,255,255,0.022) 1px, transparent 1px, transparent 3px),
        linear-gradient(180deg, #5C5E55 0%, #3D3F37 26%, #252620 54%, #191A15 72%, #35372F 100%);

      --grad-chassis: var(--material-chassis);
      --grad-panel: var(--material-panel);
      --grad-strip: var(--material-strip);
      --grad-strip-master: var(--material-master);
      --grad-control: var(--material-control);
      --grad-fader-cap:
        linear-gradient(180deg, rgba(255,255,255,0.16) 0%, transparent 25%),
        repeating-linear-gradient(90deg, rgba(255,255,255,0.028) 0px, rgba(255,255,255,0.028) 1px, transparent 1px, transparent 3px),
        linear-gradient(180deg, #5A5C53 0%, #3A3C35 38%, #21221D 50%, #3E4038 54%, #2A2B25 100%);
      --grad-fader-cap-master:
        linear-gradient(180deg, rgba(255,255,255,0.16) 0%, transparent 25%),
        repeating-linear-gradient(90deg, rgba(255,255,255,0.028) 0px, rgba(255,255,255,0.028) 1px, transparent 1px, transparent 3px),
        linear-gradient(180deg, #6B6858 0%, #464333 38%, #28261A 50%, #4E4B3B 54%, #373427 100%);
      --grad-recessed: var(--material-well);

      /* Milled bevels, chamfers and inset shadows */
      --bevel-raised:
        inset 0 1px 0 rgba(255,255,255,0.06),
        inset 0 -1px 0 rgba(0,0,0,0.5),
        0 2px 5px rgba(0,0,0,0.34),
        0 8px 22px rgba(0,0,0,0.14);
      --bevel-control:
        inset 0 1px 0 rgba(255,255,255,0.075),
        inset 0 -1px 0 rgba(0,0,0,0.55),
        0 1px 2px rgba(0,0,0,0.42);
      --bevel-recessed:
        inset 0 2px 4px rgba(0,0,0,0.78),
        inset 0 1px 0 rgba(0,0,0,0.55),
        inset 0 0 0 1px rgba(0,0,0,0.28),
        0 1px 0 rgba(255,255,255,0.035);
      --bevel-selected:
        inset 0 0 0 1px rgba(56,189,248,0.32),
        inset 0 1px 0 rgba(255,255,255,0.055),
        0 1px 2px rgba(0,0,0,0.35);

      /* Precision separators */
      --border: #393A33;
      --border-subtle: #282923;
      --border-divider: #30312B;
      --selected-border: #38BDF8;
      --focus-ring: rgba(56, 189, 248, 0.5);

      /* Typography and instrument readouts */
      --text-primary: #ECEDE7;
      --text-secondary: #ADAFA5;
      --text-muted: #777A71;
      --knob-ring: #6B6D63;
      --knob-pointer: #F3F5EF;
      --knob-rim:
        radial-gradient(circle at 33% 25%, rgba(255,255,255,0.17), transparent 36%),
        conic-gradient(from 215deg at 50% 50%,
          #2E2F2A 0deg,
          #7B7D71 34deg,
          #55574D 65deg,
          #2B2C27 112deg,
          #6D6F64 158deg,
          #383A33 215deg,
          #7A7C70 268deg,
          #35362F 310deg,
          #2E2F2A 360deg);
      --knob-face:
        radial-gradient(circle at 38% 28%,
          #43453D 0%,
          #30322B 34%,
          #1D1E1A 74%,
          #151612 100%);

      /* Functional signal colours */
      --accent-cyan: #38BDF8;
      --accent-secondary: #5EA8B6;
      --sig-green: #22C55E;
      --sig-green-dim: #132D1B;
      --sig-amber: #F59E0B;
      --sig-amber-dim: #3B2609;
      --sig-red: #EF4444;
      --sig-red-dim: #381212;

      /* Typography stacks */
      --font-ui: -apple-system, BlinkMacSystemFont, "Inter", "Segoe UI", Roboto, sans-serif;
      --font-num: "JetBrains Mono", "SFMono-Regular", Consolas, "Liberation Mono", monospace;

      /* Proportions and sizing */
      --radius-xs: 2px;
      --radius-sm: 3px;
      --touch-min: 44px;
      --header-h: 48px;
      --chbar-h: 42px;
      --nav-h: 46px;
      --slider-hit-h: 44px;
      --slider-track-h: 7px;
      --slider-thumb-w: 24px;
      --slider-thumb-h: 28px;

      /* Compact console rails — content-led widths, never stretched to fill */
      --strip-w: clamp(112px, 8.6vw, 136px);
      --strip-w-master: clamp(118px, 9.2vw, 144px);
      --strip-gap: 6px;
      --mixer-rail-pad: 8px;
    }

    *, *::before, *::after {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
      -webkit-tap-highlight-color: transparent;
    }

    html, body {
      width: 100%;
      height: 100%;
      background: var(--bg);
      color: var(--text-primary);
      font-family: var(--font-ui);
      font-size: 13px;
      line-height: 1.35;
      overflow: hidden;
      user-select: none;
      -webkit-font-smoothing: antialiased;
    }

    .num, .readout, input[type="number"], .db-val, .param-val {
      font-family: var(--font-num);
      font-variant-numeric: tabular-nums;
      letter-spacing: -0.02em;
    }

    button, select, input {
      font-family: inherit;
      font-size: inherit;
      color: inherit;
    }

    button:focus-visible, select:focus-visible, input:focus-visible, [tabindex]:focus-visible {
      outline: 2px solid var(--selected-border);
      outline-offset: 1px;
    }

    /* Console Shell Master Container
       Header, selector rail, scrollable console and persistent nav are distinct
       grid rows. This keeps the dock anchored even when mobile browser chrome
       changes the usable viewport height. */
    .console-shell {
      display: grid;
      grid-template-rows: auto auto minmax(0, 1fr) auto;
      width: 100%;
      height: 100vh; /* fallback for browsers without dynamic viewport units */
      height: 100dvh;
      min-height: 0;
      max-width: 100vw;
      overflow: hidden;
      background: var(--bg);
    }

    /* TOP BAR: Restrained Hardware Status & Command Cluster */
    .top-bar {
      min-height: var(--header-h);
      background: var(--material-chassis);
      border-bottom: 1px solid var(--border-divider);
      box-shadow:
        inset 0 -1px 0 rgba(0, 0, 0, 0.65),
        inset 0 1px 0 rgba(255, 255, 255, 0.045),
        0 2px 8px rgba(0, 0, 0, 0.42);
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 0 12px;
      gap: 10px;
      flex-shrink: 0;
      z-index: 10;
    }

    .brand-cluster {
      display: flex;
      align-items: center;
      gap: 10px;
      min-width: 0;
    }

    .brand-mark {
      font-weight: 900;
      font-size: 14px;
      letter-spacing: 1.8px;
      color: var(--text-primary);
      padding: 4px 8px;
      background: var(--material-control);
      border: 1px solid #3E3F37;
      border-radius: var(--radius-sm);
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.10),
        inset 0 -1px 0 rgba(0, 0, 0, 0.55),
        0 1px 2px rgba(0, 0, 0, 0.46);
      display: flex;
      align-items: center;
      gap: 6px;
      text-shadow: 0 1px 0 rgba(0, 0, 0, 0.8);
    }
    .brand-mark::before {
      content: "";
      width: 6px;
      height: 6px;
      background: radial-gradient(circle at 35% 30%, #BDF5FF 0%, #4CC4DE 42%, #166675 100%);
      border-radius: 50%;
      box-shadow:
        0 0 5px rgba(56, 189, 248, 0.42),
        inset 0 0 0 1px rgba(0, 0, 0, 0.35);
    }

    .selected-context-badge {
      display: flex;
      align-items: center;
      gap: 8px;
      padding: 4px 10px;
      background: var(--material-readout);
      border: 1px solid #2D2E28;
      border-left: 2px solid var(--selected-border);
      border-radius: var(--radius-sm);
      font-family: var(--font-num);
      font-size: 12px;
      font-weight: 700;
      color: var(--text-primary);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
      max-width: 240px;
      box-shadow:
        inset 0 2px 4px rgba(0, 0, 0, 0.72),
        0 1px 0 rgba(255, 255, 255, 0.03);
    }
    .selected-context-badge .tag {
      color: var(--accent-cyan);
      font-size: 11px;
    }

    /* Unified Hardware Telemetry Pod */
    .telemetry-cluster {
      display: flex;
      align-items: center;
      gap: 10px;
      flex-wrap: wrap;
    }

    .console-instrument-pod {
      display: inline-flex;
      align-items: center;
      background: var(--material-well);
      border: 1px solid #2E2F28;
      border-radius: var(--radius-sm);
      padding: 3px 8px;
      gap: 10px;
      box-shadow:
        inset 0 2px 5px rgba(0, 0, 0, 0.76),
        inset 0 0 0 1px rgba(255, 255, 255, 0.016),
        0 1px 0 rgba(255, 255, 255, 0.028);
      font-family: var(--font-num);
      font-size: 11.5px;
      font-variant-numeric: tabular-nums;
    }

    .pod-item {
      display: inline-flex;
      align-items: center;
      gap: 5px;
      color: var(--text-secondary);
      white-space: nowrap;
    }
    .pod-item strong {
      color: var(--text-primary);
      font-weight: 700;
    }
    .pod-lbl {
      color: var(--text-muted);
      font-size: 10px;
      font-weight: 700;
      text-transform: uppercase;
    }
    .pod-sep {
      width: 1px;
      height: 14px;
      background: var(--border-subtle);
    }

    .status-dot {
      width: 7px;
      height: 7px;
      border-radius: 50%;
      background: #44463F;
      flex-shrink: 0;
      box-shadow: inset 0 1px 1px rgba(0, 0, 0, 0.65), 0 1px 0 rgba(255, 255, 255, 0.035);
    }
    .status-dot.ok {
      background: radial-gradient(circle at 35% 30%, #86EFAC 0%, #22C55E 48%, #13733A 100%);
      box-shadow: 0 0 4px rgba(34, 197, 94, 0.48), inset 0 0 0 1px rgba(0, 0, 0, 0.25);
    }
    .status-dot.warn {
      background: radial-gradient(circle at 35% 30%, #FCD68A 0%, #F59E0B 48%, #9A5B06 100%);
      box-shadow: 0 0 4px rgba(245, 158, 11, 0.48), inset 0 0 0 1px rgba(0, 0, 0, 0.25);
    }
    .status-dot.err {
      background: radial-gradient(circle at 35% 30%, #FCA5A5 0%, #EF4444 48%, #991B1B 100%);
      box-shadow: 0 0 4px rgba(239, 68, 68, 0.48), inset 0 0 0 1px rgba(0, 0, 0, 0.25);
    }

    /* Precision Tactile Hardware Buttons */
    .hw-btn {
      min-height: 34px;
      padding: 0 12px;
      background: var(--material-control);
      border: 1px solid #3A3B34;
      border-radius: var(--radius-sm);
      box-shadow: var(--bevel-control);
      color: var(--text-primary);
      font-size: 11.5px;
      font-weight: 700;
      letter-spacing: 0.5px;
      text-transform: uppercase;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      gap: 6px;
      white-space: nowrap;
      transition: background 0.1s, border-color 0.1s, color 0.1s, box-shadow 0.1s, transform 0.06s;
    }
    .hw-btn:hover:not(:disabled) {
      background: var(--material-control-hover);
      border-color: #4B4D43;
    }
    .hw-btn:active:not(:disabled) {
      background: var(--material-control-pressed);
      border-color: #252620;
      box-shadow:
        inset 0 2px 4px rgba(0, 0, 0, 0.72),
        inset 0 -1px 0 rgba(255, 255, 255, 0.03),
        0 1px 0 rgba(255, 255, 255, 0.025);
      transform: translateY(1px);
    }
    .hw-btn:disabled {
      opacity: 0.38;
      cursor: not-allowed;
      box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.03), 0 1px 1px rgba(0, 0, 0, 0.25);
    }
    .hw-btn.touch {
      min-height: var(--touch-min);
      min-width: var(--touch-min);
    }
    .hw-btn.active, .hw-btn.sel {
      background: var(--material-control-pressed);
      border-color: rgba(86, 197, 214, 0.55);
      color: #FFFFFF;
      box-shadow:
        inset 0 2px 4px rgba(0, 0, 0, 0.55),
        inset 0 -2px 0 rgba(56, 189, 248, 0.62),
        inset 0 1px 0 rgba(255, 255, 255, 0.035),
        0 1px 0 rgba(255, 255, 255, 0.025);
    }

    /* Pro Audio MUTE & SOLO Backlit Hardware Buttons */
    .hw-btn.mute-btn {
      background:
        linear-gradient(180deg, rgba(255,255,255,0.045) 0%, transparent 34%),
        var(--brushed-grain),
        linear-gradient(180deg, #2E2F29 0%, #20211C 100%);
      color: var(--text-secondary);
      border-color: #40423A;
      font-weight: 800;
      letter-spacing: 0.6px;
    }
    .hw-btn.mute-btn.on {
      background:
        linear-gradient(180deg, rgba(255,255,255,0.2) 0%, transparent 38%),
        linear-gradient(180deg, #C64A45 0%, #9C2626 52%, #751A1A 100%);
      border-color: #D16A64;
      color: #FFF7F5;
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.26),
        inset 0 -2px 3px rgba(0, 0, 0, 0.34),
        0 0 0 1px rgba(239, 68, 68, 0.16),
        0 2px 4px rgba(0, 0, 0, 0.4);
    }

    .hw-btn.solo-btn {
      background:
        linear-gradient(180deg, rgba(255,255,255,0.045) 0%, transparent 34%),
        var(--brushed-grain),
        linear-gradient(180deg, #2E2F29 0%, #20211C 100%);
      color: var(--text-secondary);
      border-color: #40423A;
      font-weight: 800;
      letter-spacing: 0.6px;
    }
    .hw-btn.solo-btn.on {
      background:
        linear-gradient(180deg, rgba(255,255,255,0.2) 0%, transparent 38%),
        linear-gradient(180deg, #E9AC3D 0%, #C58212 55%, #8B5907 100%);
      border-color: #D7A044;
      color: #171208;
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.3),
        inset 0 -2px 3px rgba(0, 0, 0, 0.28),
        0 0 0 1px rgba(245, 158, 11, 0.16),
        0 2px 4px rgba(0, 0, 0, 0.4);
    }

    .hw-btn.danger {
      background:
        linear-gradient(180deg, rgba(255,255,255,0.07) 0%, transparent 34%),
        linear-gradient(180deg, #7C2626 0%, #581A1A 100%);
      border-color: #8D3A37;
      color: #FEE7E5;
    }
    .hw-btn.green-on {
      background:
        linear-gradient(180deg, rgba(255,255,255,0.06) 0%, transparent 34%),
        linear-gradient(180deg, #1A4428 0%, #12331E 100%);
      border-color: #2D6A42;
      color: #A8E9BC;
    }

    /* Quick Channel Selector Strip */
    .channel-selector-strip {
      min-height: var(--chbar-h);
      background: var(--material-chassis);
      border-bottom: 1px solid #2D2E28;
      display: flex;
      align-items: center;
      gap: 4px;
      padding: 0 10px;
      overflow-x: auto;
      overflow-y: hidden;
      flex-shrink: 0;
      scrollbar-width: thin;
      scrollbar-color: #45463F #10110F;
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.035),
        inset 0 -1px 0 rgba(0, 0, 0, 0.62);
    }
    .channel-selector-strip::-webkit-scrollbar {
      height: 5px;
    }
    .channel-selector-strip::-webkit-scrollbar-track {
      background: #0A0B09;
    }
    .channel-selector-strip::-webkit-scrollbar-thumb {
      background: #3E4038;
      border-radius: 3px;
    }

    .ch-pill {
      min-height: 30px;
      padding: 0 10px;
      background: var(--material-control);
      border: 1px solid #363730;
      border-radius: var(--radius-sm);
      color: var(--text-secondary);
      font-size: 11.5px;
      font-weight: 700;
      display: inline-flex;
      align-items: center;
      gap: 6px;
      cursor: pointer;
      white-space: nowrap;
      flex-shrink: 0;
      box-shadow: var(--bevel-control);
      transition: background 0.1s, border-color 0.1s, color 0.1s, box-shadow 0.1s;
    }
    .ch-pill:hover {
      background: var(--material-control-hover);
      color: var(--text-primary);
      border-color: #474940;
    }
    .ch-pill.selected {
      background: var(--material-control-pressed);
      border-color: rgba(86, 197, 214, 0.5);
      color: #FFFFFF;
      box-shadow:
        inset 0 2px 4px rgba(0, 0, 0, 0.6),
        inset 0 -2px 0 rgba(56, 189, 248, 0.62),
        0 1px 0 rgba(255, 255, 255, 0.025);
    }
    .ch-pill.aux {
      border-top-color: #4F7B84;
    }
    .ch-pill.master {
      border-top-color: #8B6A2E;
    }
    .ch-sep {
      width: 1px;
      height: 20px;
      background: #34362F;
      margin: 0 4px;
      flex-shrink: 0;
      box-shadow: 1px 0 0 rgba(255, 255, 255, 0.025);
    }

    /* Main Console Workspace Viewport */
    .workspace {
      flex: 1;
      min-height: 0;
      width: 100%;
      overflow-y: auto;
      overflow-x: hidden;
      overscroll-behavior-y: contain;
      position: relative;
      display: flex;
      flex-direction: column;
      background: var(--bg);
      scrollbar-width: thin;
      scrollbar-color: #3E4038 #0A0B09;
    }
    .workspace::-webkit-scrollbar {
      width: 6px;
    }
    .workspace::-webkit-scrollbar-track {
      background: #0A0B09;
    }
    .workspace::-webkit-scrollbar-thumb {
      background: #3E4038;
      border-radius: 3px;
    }

    /* 7-SCREEN CONSOLE NAVIGATION BAR */
    .main-nav {
      min-height: calc(var(--nav-h) + env(safe-area-inset-bottom, 0px));
      background: var(--material-chassis);
      border-top: 1px solid #30312B;
      display: grid;
      grid-template-columns: repeat(7, minmax(0, 1fr));
      gap: 4px;
      padding: 4px max(8px, env(safe-area-inset-right, 0px)) calc(4px + env(safe-area-inset-bottom, 0px)) max(8px, env(safe-area-inset-left, 0px));
      flex-shrink: 0;
      position: relative;
      z-index: 10;
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.04),
        inset 0 -1px 0 rgba(0, 0, 0, 0.72);
    }

    .nav-tab {
      min-height: 38px;
      background: var(--material-control);
      border: 1px solid #35362F;
      border-radius: var(--radius-sm);
      box-shadow: var(--bevel-control);
      color: var(--text-secondary);
      font-size: 12px;
      font-weight: 700;
      letter-spacing: 0.8px;
      text-transform: uppercase;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      text-align: center;
      padding: 0 6px;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
      transition: background 0.1s, border-color 0.1s, color 0.1s, box-shadow 0.1s;
    }
    .nav-tab:hover {
      background: var(--material-control-hover);
      color: var(--text-primary);
      border-color: #45473E;
    }
    .nav-tab.active {
      background: var(--material-control-pressed);
      border-color: rgba(86, 197, 214, 0.44);
      color: #FFFFFF;
      box-shadow:
        inset 0 2px 4px rgba(0, 0, 0, 0.55),
        inset 0 -3px 0 rgba(56, 189, 248, 0.66),
        0 1px 0 rgba(255, 255, 255, 0.025);
    }

    .audio-setup-shortcut {
      display: none;
    }

    /* ============================================================
       VIEW 0: COMPACT CHANNEL DECK + CONTEXTUAL WORKSPACE
       ============================================================ */
    .mixer-view {
      flex: 1;
      min-height: 0;
      height: 100%;
      width: 100%;
      display: flex;
      flex-direction: column;
      overflow: hidden;
      position: relative;
      background: var(--material-chassis);
    }

    /* Upper console deck: scrolling channel bank + docked master rail */
    .mixer-deck {
      flex: 0 0 auto;
      height: clamp(330px, 48vh, 430px);
      min-height: 0;
      display: flex;
      align-items: stretch;
      overflow: hidden;
      background: var(--material-chassis);
      border-bottom: 1px solid #2A2B25;
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.045),
        inset 0 -1px 0 rgba(0, 0, 0, 0.65),
        0 3px 10px rgba(0, 0, 0, 0.34);
      position: relative;
    }

    .mixer-bank {
      flex: 1;
      min-width: 0;
      display: flex;
      align-items: stretch;
      overflow-x: auto;
      overflow-y: hidden;
      overscroll-behavior-x: contain;
      padding: var(--mixer-rail-pad);
      scrollbar-width: thin;
      scrollbar-color: #3E4038 #0A0B09;
      background:
        linear-gradient(180deg, rgba(0, 0, 0, 0.32) 0%, transparent 10%, transparent 88%, rgba(0, 0, 0, 0.3) 100%),
        linear-gradient(90deg, rgba(0, 0, 0, 0.16) 0%, transparent 5%, transparent 95%, rgba(0, 0, 0, 0.18) 100%),
        var(--brushed-grain),
        linear-gradient(180deg, #151611 0%, #11120E 100%);
      box-shadow:
        inset 0 3px 8px rgba(0, 0, 0, 0.34),
        inset 0 -1px 0 rgba(255, 255, 255, 0.03);
    }
    .mixer-bank::-webkit-scrollbar {
      height: 8px;
    }
    .mixer-bank::-webkit-scrollbar-track {
      background: #0A0B09;
    }
    .mixer-bank::-webkit-scrollbar-thumb {
      background: #3E4038;
      border-radius: 4px;
    }

    .strip-rail {
      display: flex;
      align-items: stretch;
      gap: var(--strip-gap);
      min-width: min-content;
    }

    .mixer-master-bay {
      flex: 0 0 auto;
      display: flex;
      align-items: stretch;
      padding: var(--mixer-rail-pad) var(--mixer-rail-pad) calc(var(--mixer-rail-pad) + var(--bank-scrollbar, 0px)) 6px;
      background: var(--material-master-bay);
      border-left: 1px solid #595345;
      box-shadow:
        -5px 0 16px rgba(0, 0, 0, 0.5),
        inset 1px 0 0 rgba(255, 255, 255, 0.065);
      position: relative;
      z-index: 5;
    }

    .mixer-empty {
      flex: 0 0 auto;
      width: 244px;
      min-height: 100%;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      gap: 12px;
      padding: 16px;
      background: var(--grad-panel);
      border: 1px solid var(--border-subtle);
      border-radius: var(--radius-sm);
      box-shadow: var(--bevel-raised);
      text-align: center;
    }

    .engine-state-empty {
      flex: 1;
      min-height: 180px;
      margin: 12px;
      align-items: center;
      justify-content: center;
      text-align: center;
    }

    /* Channel Strip Module */
    .channel-strip {
      flex: 0 0 auto;
      width: var(--strip-w);
      min-width: var(--strip-w);
      max-width: var(--strip-w);
      height: 100%;
      background: var(--material-strip);
      border: 1px solid #373831;
      border-radius: var(--radius-sm);
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.10),
        inset 1px 0 0 rgba(255, 255, 255, 0.025),
        inset -1px 0 0 rgba(0, 0, 0, 0.35),
        inset 0 -1px 0 rgba(0, 0, 0, 0.58),
        0 3px 8px rgba(0, 0, 0, 0.32);
      display: flex;
      flex-direction: column;
      padding: 7px 6px;
      gap: 6px;
      position: relative;
      transition: border-color 0.15s, box-shadow 0.15s, background 0.15s;
    }
    .channel-strip.selected {
      background:
        linear-gradient(180deg, rgba(56, 189, 248, 0.085) 0%, rgba(56, 189, 248, 0.018) 20%, transparent 44%),
        var(--material-strip);
      border-color: rgba(86, 197, 214, 0.55);
      box-shadow:
        inset 0 0 0 1px rgba(56, 189, 248, 0.12),
        inset 0 1px 0 rgba(255, 255, 255, 0.08),
        inset 0 -1px 0 rgba(0, 0, 0, 0.5),
        0 2px 7px rgba(0, 0, 0, 0.32);
    }
    .channel-strip.selected::after {
      content: "";
      position: absolute;
      top: -1px;
      left: 8px;
      right: 8px;
      height: 1px;
      background: linear-gradient(90deg, transparent 0%, rgba(56, 189, 248, 0.85) 16%, rgba(56, 189, 248, 0.85) 84%, transparent 100%);
      border-radius: 0 0 1px 1px;
      box-shadow: 0 0 5px rgba(56, 189, 248, 0.22);
      pointer-events: none;
    }
    .channel-strip.master-strip {
      width: var(--strip-w-master);
      min-width: var(--strip-w-master);
      max-width: var(--strip-w-master);
      background: var(--material-master);
      border-color: #514C3E;
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.105),
        inset 1px 0 0 rgba(255, 255, 255, 0.028),
        inset -1px 0 0 rgba(0, 0, 0, 0.36),
        inset 0 -1px 0 rgba(0, 0, 0, 0.62),
        0 3px 9px rgba(0, 0, 0, 0.36);
    }
    .channel-strip.master-strip.selected {
      background:
        linear-gradient(180deg, rgba(56, 189, 248, 0.075) 0%, rgba(56, 189, 248, 0.014) 20%, transparent 44%),
        var(--material-master);
      border-color: rgba(86, 197, 214, 0.58);
    }
    .master-strip .strip-num-badge {
      color: var(--sig-amber);
    }

    /* Strip Header / Selection Target */
    .strip-head {
      width: 100%;
      min-height: 36px;
      background:
        linear-gradient(180deg, rgba(255, 255, 255, 0.015) 0%, transparent 30%),
        var(--material-readout);
      border: 1px solid #24251F;
      border-radius: var(--radius-xs);
      padding: 4px 5px;
      display: flex;
      flex-direction: column;
      justify-content: center;
      gap: 1px;
      cursor: pointer;
      text-align: left;
      color: inherit;
      box-shadow:
        inset 0 2px 4px rgba(0, 0, 0, 0.78),
        inset 0 0 0 1px rgba(255, 255, 255, 0.014),
        0 1px 0 rgba(255, 255, 255, 0.035);
      transition: border-color 0.1s, background 0.1s, box-shadow 0.1s;
    }
    .strip-head:hover {
      border-color: #3D3F37;
      background:
        linear-gradient(180deg, rgba(255, 255, 255, 0.025) 0%, transparent 32%),
        #0D0E0B;
    }
    .strip-head:focus-visible {
      outline: 2px solid var(--selected-border);
      outline-offset: 1px;
    }
    .strip-head-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 3px;
      min-width: 0;
    }
    .strip-num-badge {
      font-size: 12px;
      font-family: var(--font-num);
      font-weight: 800;
      font-variant-numeric: tabular-nums;
      color: var(--accent-cyan);
      line-height: 1.1;
      white-space: nowrap;
    }
    .strip-format-tag {
      font-size: 8px;
      font-family: var(--font-num);
      font-weight: 700;
      color: #8A8C83;
      line-height: 1.1;
      padding: 1px 3px;
      background: #0B0C09;
      border: 1px solid #282923;
      border-radius: 1px;
      white-space: nowrap;
      box-shadow: inset 0 1px 1px rgba(0, 0, 0, 0.65);
    }
    .strip-name {
      font-size: 11.5px;
      font-weight: 800;
      color: var(--text-primary);
      line-height: 1.2;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
      letter-spacing: 0.2px;
      max-width: 100%;
      text-shadow: 0 1px 0 rgba(0, 0, 0, 0.55);
    }
    .master-strip .strip-name {
      letter-spacing: 0.35px;
    }

    /* Compact Processing Indicators */
    .strip-badges {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 3px;
      flex-shrink: 0;
    }
    .proc-badge {
      appearance: none;
      font-family: var(--font-num);
      font-size: 8.5px;
      font-weight: 800;
      text-align: center;
      min-height: 20px;
      padding: 2px 1px;
      line-height: 1;
      background:
        linear-gradient(180deg, rgba(255, 255, 255, 0.025) 0%, transparent 48%),
        linear-gradient(180deg, #191A16 0%, #10110E 100%);
      border: 1px solid #2B2C26;
      border-radius: var(--radius-xs);
      color: #72756B;
      cursor: pointer;
      white-space: nowrap;
      overflow: hidden;
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.035),
        inset 0 -1px 0 rgba(0, 0, 0, 0.5);
      transition: color 0.1s, border-color 0.1s, background 0.1s, box-shadow 0.1s;
    }
    .proc-badge:hover {
      border-color: #4A4C43;
      color: #C4C6BD;
    }
    .proc-badge.on {
      color: #D5F6FB;
      border-color: #3C6B73;
      background:
        linear-gradient(180deg, rgba(125, 226, 242, 0.08) 0%, transparent 50%),
        linear-gradient(180deg, #192C30 0%, #101D20 100%);
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.055),
        inset 0 -1px 0 rgba(0, 0, 0, 0.46),
        0 0 0 1px rgba(56, 189, 248, 0.045);
    }
    .proc-badge:disabled {
      cursor: default;
      opacity: 0.45;
    }

    /* Fader + Precision Meter Well */
    .fader-meter-bay {
      flex: 1 1 auto;
      min-height: 130px;
      display: flex;
      gap: 4px;
      background: var(--material-well);
      border: 1px solid #1D1E19;
      border-radius: var(--radius-xs);
      padding: 7px 5px;
      align-items: stretch;
      box-shadow:
        inset 0 3px 8px rgba(0, 0, 0, 0.78),
        inset 0 0 0 1px rgba(255, 255, 255, 0.018),
        0 1px 0 rgba(255, 255, 255, 0.035);
    }

    .v-fader-track {
      position: relative;
      flex: 1;
      min-width: 36px;
      height: 100%;
      min-height: 118px;
      cursor: ns-resize;
      touch-action: none;
      display: flex;
      justify-content: center;
      align-items: stretch;
    }
    .v-fader-track::before {
      content: "";
      position: absolute;
      top: 2px;
      bottom: 2px;
      left: calc(50% - 13px);
      width: 26px;
      border-radius: 5px;
      background:
        linear-gradient(90deg,
          rgba(0, 0, 0, 0.32) 0%,
          rgba(255, 255, 255, 0.016) 24%,
          transparent 46%,
          transparent 54%,
          rgba(255, 255, 255, 0.026) 78%,
          rgba(0, 0, 0, 0.22) 100%);
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.025),
        inset 0 -1px 0 rgba(0, 0, 0, 0.28);
      pointer-events: none;
    }
    .v-fader-scale {
      position: absolute;
      left: 1px;
      top: 14px;
      bottom: 14px;
      width: 15px;
      display: block;
      font-family: var(--font-num);
      font-size: 7.5px;
      font-weight: 700;
      font-variant-numeric: tabular-nums;
      color: #898C82;
      pointer-events: none;
      user-select: none;
      text-shadow: 0 1px 0 rgba(0, 0, 0, 0.7);
    }
    .v-fader-scale span {
      position: absolute;
      left: 0;
      width: 100%;
      transform: translateY(-50%);
      line-height: 1;
      white-space: nowrap;
    }
    .v-fader-scale span::after {
      content: "";
      position: absolute;
      left: calc(100% + 1px);
      top: 50%;
      width: 2px;
      height: 1px;
      background: rgba(228, 231, 222, 0.18);
    }
    .v-fader-scale .u-mark {
      color: var(--accent-cyan);
      font-weight: 900;
      text-shadow: 0 1px 0 rgba(0, 0, 0, 0.65);
    }
    .v-fader-scale .u-mark::after {
      background: rgba(56, 189, 248, 0.72);
    }

    .v-fader-slot {
      width: 6px;
      height: calc(100% - 28px);
      margin-top: 14px;
      background:
        linear-gradient(90deg, #020302 0%, #0A0B09 34%, #050604 50%, #11120E 76%, #050604 100%);
      border: 1px solid #171813;
      border-radius: 2px;
      position: relative;
      box-shadow:
        inset 0 3px 6px rgba(0, 0, 0, 0.95),
        inset 1px 0 1px rgba(0, 0, 0, 0.9),
        0 0 0 1px rgba(255, 255, 255, 0.018),
        0 1px 0 rgba(255, 255, 255, 0.025);
    }
    .v-fader-unity {
      position: absolute;
      left: 17px;
      right: 1px;
      top: calc(14px + (1 - 0.86) * (100% - 28px));
      height: 1px;
      background: rgba(56, 189, 248, 0.58);
      pointer-events: none;
    }
    .v-fader-cap {
      position: absolute;
      left: 50%;
      transform: translate(-50%, -50%);
      width: 36px;
      height: 22px;
      background: var(--grad-fader-cap);
      border: 1px solid #1E1F1A;
      border-radius: 2px;
      box-shadow:
        0 4px 8px rgba(0, 0, 0, 0.7),
        0 1px 2px rgba(0, 0, 0, 0.8),
        inset 0 1px 0 rgba(255, 255, 255, 0.2),
        inset 0 -2px 3px rgba(0, 0, 0, 0.52);
      pointer-events: none;
      display: flex;
      align-items: center;
      justify-content: center;
      isolation: isolate;
    }
    .v-fader-cap::before {
      content: "";
      position: absolute;
      left: 5px;
      right: 5px;
      top: 8px;
      height: 6px;
      border-radius: 1px;
      background: #0A0B09;
      box-shadow:
        inset 0 1px 2px rgba(0, 0, 0, 0.9),
        0 1px 0 rgba(255, 255, 255, 0.055);
      pointer-events: none;
      z-index: 0;
    }
    .v-fader-cap::after {
      content: "";
      position: relative;
      width: 23px;
      height: 2px;
      border-radius: 1px;
      background: #F4F6F0;
      box-shadow:
        0 0 1px rgba(0, 0, 0, 0.9),
        0 1px 1px rgba(0, 0, 0, 0.65);
      z-index: 1;
    }
    .master-strip .v-fader-cap {
      background: var(--grad-fader-cap-master);
      border-color: #2B281E;
    }
    .master-strip .v-fader-cap::before {
      background: #0D0B07;
    }
    .master-strip .v-fader-cap::after {
      background: #F0C777;
      box-shadow: 0 0 1px rgba(0, 0, 0, 0.9), 0 1px 1px rgba(0, 0, 0, 0.65);
    }
    .v-fader-track.dragging .v-fader-cap {
      border-color: rgba(86, 197, 214, 0.7);
      box-shadow:
        0 4px 8px rgba(0, 0, 0, 0.72),
        0 0 0 1px rgba(56, 189, 248, 0.24),
        inset 0 1px 0 rgba(255, 255, 255, 0.2),
        inset 0 -2px 3px rgba(0, 0, 0, 0.52);
    }

    .meter-column {
      display: flex;
      flex-direction: column;
      gap: 3px;
      width: 42px;
      flex: 0 0 42px;
      min-height: 0;
      align-self: stretch;
    }
    .clip-led {
      height: 12px;
      font-size: 7.5px;
      font-weight: 900;
      font-family: var(--font-num);
      text-align: center;
      line-height: 11px;
      background:
        linear-gradient(180deg, rgba(255, 255, 255, 0.018) 0%, transparent 45%),
        #12130F;
      border: 1px solid #272822;
      color: #73766C;
      border-radius: 1px;
      box-shadow:
        inset 0 1px 3px rgba(0, 0, 0, 0.75),
        0 1px 0 rgba(255, 255, 255, 0.025);
    }
    .clip-led.hot {
      background:
        linear-gradient(180deg, #E2584F 0%, #B92626 60%, #8C1717 100%);
      border-color: #D16A64;
      color: #FFF8F5;
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.28),
        inset 0 -2px 2px rgba(0, 0, 0, 0.28),
        0 0 0 1px rgba(239, 68, 68, 0.12);
    }
    .strip-meter-canvas {
      flex: 1 1 0;
      width: 100%;
      min-height: 100px;
      display: block;
      background: #060705;
      border: 1px solid #1B1C17;
      border-radius: 1px;
      box-shadow:
        inset 0 0 0 1px rgba(0, 0, 0, 0.5),
        0 1px 0 rgba(255, 255, 255, 0.025);
    }

    .strip-gr-meter {
      flex: 0 0 23px;
      min-width: 0;
      display: grid;
      grid-template-rows: 10px 5px;
      gap: 2px;
      padding: 2px 3px;
      background: linear-gradient(180deg, #11120F, #070806);
      border: 1px solid #252720;
      border-radius: 1px;
      color: #8C8F84;
      font-family: var(--font-num);
      font-size: 7px;
      font-weight: 800;
      line-height: 1;
      font-variant-numeric: tabular-nums;
    }
    .strip-gr-meta {
      min-width: 0;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 2px;
      white-space: nowrap;
    }
    .strip-gr-value {
      color: #AEB1A6;
      overflow: hidden;
      text-overflow: clip;
    }
    .strip-gr-meter.active .strip-gr-value {
      color: var(--sig-amber);
    }
    .strip-gr-track {
      position: relative;
      overflow: hidden;
      background: #030403;
      border: 1px solid #1D1E18;
      box-shadow: inset 0 1px 2px rgba(0, 0, 0, 0.8);
    }
    .strip-gr-fill {
      display: block;
      width: 0;
      height: 100%;
      background: linear-gradient(90deg, #B77716, #F2B544);
      box-shadow: 0 0 3px rgba(245, 158, 11, 0.35);
      transition: width 80ms linear;
    }
    .strip-gr-meter.unavailable .strip-gr-fill {
      width: 0 !important;
      background: #62645C;
      box-shadow: none;
    }

    .strip-db-readout {
      text-align: center;
      font-family: var(--font-num);
      font-size: 11px;
      font-weight: 800;
      font-variant-numeric: tabular-nums;
      color: #EAECE5;
      background: var(--material-readout);
      border: 1px solid #272822;
      padding: 3px 4px;
      border-radius: var(--radius-xs);
      line-height: 1.2;
      box-shadow:
        inset 0 2px 4px rgba(0, 0, 0, 0.72),
        inset 0 0 0 1px rgba(255, 255, 255, 0.012),
        0 1px 0 rgba(255, 255, 255, 0.03);
    }

    .strip-btns {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 4px;
      flex-shrink: 0;
    }
    .strip-btns .hw-btn {
      min-height: 38px;
      padding: 0 2px;
      font-size: 10.5px;
      letter-spacing: 0.2px;
    }

    .strip-source-row,
    .strip-gain-control,
    .strip-pan-control,
    .strip-select-btn,
    .strip-add-btn,
    .strip-master-solo {
      display: none;
    }

    /* Machined Rotary Knob (selected-channel workspace) */
    .rotary-dial {
      width: 42px;
      height: 42px;
      border-radius: 50%;
      background: var(--knob-rim);
      border: 1px solid #0B0C09;
      box-shadow:
        0 4px 8px rgba(0, 0, 0, 0.62),
        0 1px 2px rgba(0, 0, 0, 0.8),
        inset 0 1px 0 rgba(255, 255, 255, 0.12),
        inset 0 -1px 1px rgba(0, 0, 0, 0.78);
      position: relative;
      cursor: ns-resize;
      touch-action: none;
      transition: box-shadow 0.12s, border-color 0.12s;
    }
    .rotary-dial::before {
      content: "";
      position: absolute;
      inset: 4px;
      border-radius: 50%;
      background: var(--knob-face);
      border: 1px solid #3D3F37;
      box-shadow:
        inset 0 1px 1px rgba(255, 255, 255, 0.08),
        inset 0 -3px 6px rgba(0, 0, 0, 0.65),
        0 1px 2px rgba(0, 0, 0, 0.48);
      pointer-events: none;
      z-index: 0;
    }
    .rotary-dial::after {
      content: "";
      position: absolute;
      inset: 1px;
      border-radius: 50%;
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.09),
        inset 0 -1px 0 rgba(0, 0, 0, 0.55);
      pointer-events: none;
      z-index: 1;
    }
    .rotary-dial:hover {
      box-shadow:
        0 4px 8px rgba(0, 0, 0, 0.62),
        0 0 0 1px rgba(255, 255, 255, 0.055),
        inset 0 1px 0 rgba(255, 255, 255, 0.12),
        inset 0 -1px 1px rgba(0, 0, 0, 0.78);
    }
    .rotary-dial.dragging {
      border-color: rgba(86, 197, 214, 0.5);
      box-shadow:
        0 4px 8px rgba(0, 0, 0, 0.62),
        0 0 0 1px rgba(56, 189, 248, 0.24),
        inset 0 1px 0 rgba(255, 255, 255, 0.12),
        inset 0 -1px 1px rgba(0, 0, 0, 0.78);
    }
    .rotary-dial:focus-visible {
      outline: 2px solid var(--selected-border);
      outline-offset: 2px;
    }
    .rotary-dial[aria-disabled="true"] {
      opacity: 0.45;
      cursor: not-allowed;
    }
    .rotary-pointer {
      position: absolute;
      top: 5px;
      left: calc(50% - 1px);
      width: 2px;
      height: 16px;
      background: linear-gradient(180deg, #FBFCF8 0%, #D9DBD3 58%, #90938A 100%);
      border-radius: 1px;
      box-shadow:
        0 1px 1px rgba(0, 0, 0, 0.8),
        0 0 0 1px rgba(0, 0, 0, 0.1);
      transform-origin: 50% 15px;
      pointer-events: none;
      z-index: 2;
    }

    /* ============================================================
       SELECTED-CHANNEL WORKSPACE
       ============================================================ */
    .mixer-workspace {
      flex: 1;
      min-width: 0;
      min-height: 0;
      display: flex;
      flex-direction: column;
      overflow: hidden;
      background: var(--material-panel);
      box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.035);
    }

    .work-head {
      flex: 0 0 auto;
      min-height: 38px;
      padding: 4px 10px;
      display: flex;
      align-items: center;
      gap: 12px;
      background:
        linear-gradient(180deg, rgba(255, 255, 255, 0.035) 0%, transparent 38%),
        var(--brushed-grain),
        linear-gradient(180deg, #24251F 0%, #191A15 100%);
      border-bottom: 1px solid #292A24;
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.04),
        0 1px 0 rgba(0, 0, 0, 0.5);
    }
    .work-ident {
      display: flex;
      align-items: baseline;
      gap: 7px;
      min-width: 0;
      flex-shrink: 0;
    }
    .work-tag {
      font-family: var(--font-num);
      font-size: 12px;
      font-weight: 800;
      color: var(--accent-cyan);
      white-space: nowrap;
    }
    .work-name {
      font-size: 13px;
      font-weight: 800;
      color: var(--text-primary);
      letter-spacing: 0.25px;
      max-width: 190px;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
      text-shadow: 0 1px 0 rgba(0, 0, 0, 0.55);
    }
    .work-type {
      font-family: var(--font-num);
      font-size: 8.5px;
      font-weight: 700;
      color: #91948A;
      background: var(--material-readout);
      border: 1px solid #292A24;
      border-radius: 1px;
      padding: 1px 4px;
      white-space: nowrap;
      box-shadow: inset 0 1px 1px rgba(0, 0, 0, 0.6);
    }
    .work-meta {
      flex: 1;
      min-width: 0;
      display: flex;
      align-items: center;
      gap: 11px;
      overflow: hidden;
    }
    .work-meta-item {
      display: flex;
      align-items: baseline;
      gap: 4px;
      white-space: nowrap;
      min-width: 0;
    }
    .work-meta-item span {
      font-size: 8px;
      font-weight: 800;
      letter-spacing: 0.5px;
      text-transform: uppercase;
      color: var(--text-muted);
    }
    .work-meta-item b {
      font-family: var(--font-num);
      font-size: 10.5px;
      font-weight: 700;
      font-variant-numeric: tabular-nums;
      color: var(--text-primary);
      white-space: nowrap;
    }
    .work-actions {
      margin-left: auto;
      display: flex;
      align-items: center;
      gap: 5px;
      flex-shrink: 0;
    }
    .work-actions .hw-btn {
      min-height: 30px;
      padding: 0 8px;
      font-size: 10px;
      letter-spacing: 0.2px;
    }

    .work-body {
      flex: 1;
      min-height: 0;
      display: grid;
      grid-template-columns:
        minmax(185px, 0.82fr)
        minmax(205px, 0.92fr)
        minmax(240px, 1.65fr)
        minmax(225px, 1.08fr);
      gap: 8px;
      padding: 8px 10px 10px;
      overflow-y: auto;
      overflow-x: hidden;
      scrollbar-width: thin;
      scrollbar-color: #3E4038 #0D0E0B;
      background:
        linear-gradient(180deg, rgba(0, 0, 0, 0.18) 0%, transparent 8%, transparent 92%, rgba(0, 0, 0, 0.16) 100%),
        linear-gradient(180deg, #151611 0%, #10110D 100%);
      box-shadow: inset 0 1px 4px rgba(0, 0, 0, 0.28);
    }

    .work-module {
      min-width: 0;
      min-height: 0;
      display: flex;
      flex-direction: column;
      gap: 6px;
      padding: 7px 8px;
      background: var(--material-panel);
      border: 1px solid #34352E;
      border-radius: var(--radius-xs);
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.06),
        inset 1px 0 0 rgba(255, 255, 255, 0.018),
        inset -1px 0 0 rgba(0, 0, 0, 0.24),
        inset 0 -1px 0 rgba(0, 0, 0, 0.44),
        0 1px 2px rgba(0, 0, 0, 0.28);
      overflow-y: auto;
      scrollbar-width: thin;
      scrollbar-color: #3E4038 #171815;
    }
    .work-channel {
      background: var(--material-strip);
      border-color: #373831;
    }
    .work-eq {
      background:
        linear-gradient(180deg, rgba(255, 255, 255, 0.026) 0%, transparent 24%),
        linear-gradient(180deg, #1D1E18 0%, #151611 100%);
      border-color: #292A23;
    }
    .work-module-head {
      flex: 0 0 auto;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 6px;
      min-height: 18px;
      padding-bottom: 4px;
      background: linear-gradient(180deg, rgba(255, 255, 255, 0.028) 0%, transparent 100%);
      border-bottom: 1px solid #2A2B25;
      border-radius: 1px;
      box-shadow: 0 1px 0 rgba(255, 255, 255, 0.022);
    }
    .work-module-title {
      font-size: 9px;
      font-weight: 800;
      letter-spacing: 0.85px;
      text-transform: uppercase;
      color: var(--text-secondary);
      white-space: nowrap;
    }
    .work-module-note {
      font-family: var(--font-num);
      font-size: 8.5px;
      font-weight: 700;
      color: var(--text-muted);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .work-link {
      appearance: none;
      background: none;
      border: 0;
      color: var(--accent-secondary);
      font-family: var(--font-num);
      font-size: 8.5px;
      font-weight: 800;
      letter-spacing: 0.3px;
      cursor: pointer;
      padding: 2px 0;
      min-height: 22px;
      white-space: nowrap;
    }
    .work-link:hover {
      color: #9EE1EC;
    }

    /* Left module: selected-channel gain / pan / inserts */
    .work-knobs {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 6px;
      align-items: center;
      justify-items: center;
      flex: 0 0 auto;
    }
    .work-knob {
      min-width: 0;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 3px;
    }
    .work-knob-lbl {
      font-size: 8px;
      font-weight: 800;
      letter-spacing: 0.45px;
      text-transform: uppercase;
      color: var(--text-secondary);
      white-space: nowrap;
    }
    .work-knob-val {
      font-family: var(--font-num);
      font-size: 10px;
      font-weight: 700;
      font-variant-numeric: tabular-nums;
      color: var(--text-primary);
      white-space: nowrap;
    }
    .work-switch-row {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 4px;
      flex: 0 0 auto;
    }
    .work-switch-row .hw-btn {
      min-height: 27px;
      padding: 0 4px;
      font-size: 9px;
      letter-spacing: 0.1px;
    }
    .work-inline {
      margin-top: auto;
      display: flex;
      flex-direction: column;
      gap: 3px;
      flex: 0 0 auto;
      padding-top: 4px;
      border-top: 1px solid #2E2F2B;
    }
    .work-inline-label {
      font-size: 8px;
      font-weight: 800;
      letter-spacing: 0.5px;
      text-transform: uppercase;
      color: var(--text-muted);
    }
    .work-insert-btn {
      appearance: none;
      width: 100%;
      min-height: 26px;
      padding: 0 6px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 5px;
      background:
        linear-gradient(180deg, rgba(255, 255, 255, 0.018) 0%, transparent 50%),
        var(--material-readout);
      border: 1px solid #2A2B25;
      border-radius: var(--radius-xs);
      color: var(--text-secondary);
      font-size: 9px;
      font-weight: 700;
      text-align: left;
      cursor: pointer;
      overflow: hidden;
      white-space: nowrap;
      box-shadow:
        inset 0 1px 3px rgba(0, 0, 0, 0.72),
        0 1px 0 rgba(255, 255, 255, 0.025);
      transition: border-color 0.1s, color 0.1s, box-shadow 0.1s;
    }
    .work-insert-btn:hover {
      border-color: #496F77;
      color: var(--text-primary);
      box-shadow:
        inset 0 1px 3px rgba(0, 0, 0, 0.72),
        inset 0 0 0 1px rgba(56, 189, 248, 0.07),
        0 1px 0 rgba(255, 255, 255, 0.03);
    }
    .work-insert-btn span:first-child {
      min-width: 0;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    /* Master limiter quick controls */
    .work-control-stack {
      display: flex;
      flex-direction: column;
      gap: 7px;
      min-height: 0;
    }
    .work-control-line {
      display: flex;
      flex-direction: column;
      gap: 1px;
      min-width: 0;
    }
    .work-control-head {
      display: flex;
      align-items: baseline;
      justify-content: space-between;
      gap: 5px;
      font-size: 8px;
      font-weight: 800;
      letter-spacing: 0.4px;
      text-transform: uppercase;
      color: var(--text-muted);
      white-space: nowrap;
    }
    .work-control-head b {
      font-family: var(--font-num);
      font-size: 9.5px;
      font-weight: 700;
      font-variant-numeric: tabular-nums;
      color: var(--text-primary);
      letter-spacing: 0;
      text-transform: none;
    }

    /* Processing module */
    .work-proc-list {
      min-height: 0;
      display: flex;
      flex-direction: column;
      gap: 2px;
      overflow-y: auto;
      scrollbar-width: none;
    }
    .work-proc-list::-webkit-scrollbar {
      display: none;
    }
    .work-proc-row {
      display: flex;
      align-items: center;
      gap: 5px;
      min-height: 24px;
      border-bottom: 1px solid rgba(255, 255, 255, 0.035);
    }
    .work-proc-row:last-child {
      border-bottom: 0;
    }
    .work-toggle {
      appearance: none;
      flex: 0 0 54px;
      min-height: 22px;
      padding: 0 3px;
      background:
        linear-gradient(180deg, rgba(255, 255, 255, 0.015) 0%, transparent 48%),
        var(--material-readout);
      border: 1px solid #2A2B25;
      border-radius: var(--radius-xs);
      color: #73766C;
      font-family: var(--font-num);
      font-size: 8.5px;
      font-weight: 800;
      letter-spacing: 0.25px;
      cursor: pointer;
      white-space: nowrap;
      box-shadow:
        inset 0 1px 3px rgba(0, 0, 0, 0.62),
        0 1px 0 rgba(255, 255, 255, 0.025);
      transition: border-color 0.1s, color 0.1s, background 0.1s, box-shadow 0.1s;
    }
    .work-toggle:hover {
      border-color: #4C4E44;
      color: var(--text-secondary);
    }
    .work-toggle.on {
      background:
        linear-gradient(180deg, rgba(134, 239, 172, 0.08) 0%, transparent 52%),
        linear-gradient(180deg, #173022 0%, #102218 100%);
      border-color: #35694A;
      color: #B7EFCB;
      box-shadow:
        inset 0 1px 2px rgba(0, 0, 0, 0.42),
        inset 0 -1px 0 rgba(34, 197, 94, 0.32),
        0 1px 0 rgba(255, 255, 255, 0.025);
    }
    .work-proc-val {
      flex: 1;
      min-width: 0;
      font-family: var(--font-num);
      font-size: 9px;
      font-weight: 700;
      color: var(--text-secondary);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .work-gr {
      flex: 0 0 auto;
      min-width: 29px;
      text-align: right;
      font-family: var(--font-num);
      font-size: 8.5px;
      font-weight: 700;
      font-variant-numeric: tabular-nums;
      color: var(--text-muted);
      white-space: nowrap;
    }
    .work-gr.hot {
      color: var(--sig-amber);
    }
    .work-gr.unavailable {
      color: #70736A;
    }

    .dyn-gr-readout {
      flex: 0 0 auto;
      min-height: 30px;
      display: grid;
      grid-template-columns: 92px minmax(50px, 1fr) 58px;
      align-items: center;
      gap: 8px;
      padding: 4px 8px;
      background: linear-gradient(180deg, #11120F, #070806);
      border: 1px solid #292A23;
      border-radius: 1px;
      box-shadow: inset 0 1px 3px rgba(0, 0, 0, 0.72), 0 1px 0 rgba(255, 255, 255, 0.025);
      color: var(--text-secondary);
      font-family: var(--font-num);
      font-size: 9px;
      font-weight: 800;
      font-variant-numeric: tabular-nums;
    }
    .dyn-gr-label {
      white-space: nowrap;
      letter-spacing: 0.35px;
    }
    .dyn-gr-track {
      height: 7px;
      overflow: hidden;
      background: #030403;
      border: 1px solid #1D1E18;
      box-shadow: inset 0 1px 2px rgba(0, 0, 0, 0.8);
    }
    .dyn-gr-fill {
      display: block;
      width: 0;
      height: 100%;
      background: linear-gradient(90deg, #B77716, #F2B544);
      transition: width 80ms linear;
    }
    .dyn-gr-value {
      color: #AEB1A6;
      text-align: right;
      white-space: nowrap;
    }
    .dyn-gr-readout.active .dyn-gr-value {
      color: var(--sig-amber);
    }
    .dyn-gr-readout.unavailable .dyn-gr-fill {
      width: 0 !important;
      background: #62645C;
    }

    /* EQ + RTA module */
    .work-eq-wrap {
      position: relative;
      flex: 1;
      min-height: 84px;
      background:
        radial-gradient(110% 80% at 50% 0%, rgba(255, 255, 255, 0.028), transparent 72%),
        linear-gradient(180deg, #090A08 0%, #050604 100%);
      border: 1px solid #171813;
      border-radius: 1px;
      overflow: hidden;
      cursor: pointer;
      box-shadow:
        inset 0 3px 8px rgba(0, 0, 0, 0.82),
        inset 0 0 0 1px rgba(255, 255, 255, 0.018),
        0 1px 0 rgba(255, 255, 255, 0.035);
      transition: border-color 0.1s, box-shadow 0.1s;
    }
    .work-eq-wrap:hover {
      border-color: #3D626B;
      box-shadow:
        inset 0 3px 8px rgba(0, 0, 0, 0.82),
        inset 0 0 0 1px rgba(56, 189, 248, 0.05),
        0 1px 0 rgba(255, 255, 255, 0.04);
    }
    .work-eq-canvas {
      width: 100%;
      height: 100%;
      display: block;
    }

    /* Routing / sends module */
    .work-route-stack {
      display: flex;
      flex-direction: column;
      gap: 3px;
      min-height: 0;
    }
    .work-route-line {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 6px;
      min-height: 24px;
    }
    .work-route-lbl {
      font-size: 8.5px;
      font-weight: 800;
      letter-spacing: 0.35px;
      text-transform: uppercase;
      color: var(--text-muted);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .work-route-btn {
      appearance: none;
      flex-shrink: 0;
      max-width: 132px;
      min-height: 24px;
      padding: 0 5px;
      background:
        linear-gradient(180deg, rgba(255, 255, 255, 0.015) 0%, transparent 48%),
        var(--material-readout);
      border: 1px solid #2A2B25;
      border-radius: var(--radius-xs);
      color: var(--text-secondary);
      font-family: var(--font-num);
      font-size: 8.5px;
      font-weight: 800;
      font-variant-numeric: tabular-nums;
      cursor: pointer;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
      box-shadow:
        inset 0 1px 3px rgba(0, 0, 0, 0.6),
        0 1px 0 rgba(255, 255, 255, 0.025);
      transition: border-color 0.1s, color 0.1s, background 0.1s, box-shadow 0.1s;
    }
    .work-route-btn:hover {
      border-color: #4C4E44;
    }
    .work-route-btn.on {
      background:
        linear-gradient(180deg, rgba(134, 239, 172, 0.08) 0%, transparent 52%),
        linear-gradient(180deg, #173022 0%, #102218 100%);
      border-color: #35694A;
      color: #B7EFCB;
      box-shadow:
        inset 0 1px 2px rgba(0, 0, 0, 0.42),
        inset 0 -1px 0 rgba(34, 197, 94, 0.3),
        0 1px 0 rgba(255, 255, 255, 0.025);
    }
    .work-sends {
      display: flex;
      flex-direction: column;
      gap: 2px;
      min-height: 0;
    }
    .work-send-row {
      display: grid;
      grid-template-columns: 17px minmax(0, 1fr) 27px 28px;
      align-items: center;
      gap: 3px;
      min-height: 25px;
    }
    .work-send-lbl {
      font-family: var(--font-num);
      font-size: 8px;
      font-weight: 800;
      color: var(--text-muted);
      white-space: nowrap;
    }
    .work-send-val {
      font-family: var(--font-num);
      font-size: 8.5px;
      font-weight: 700;
      font-variant-numeric: tabular-nums;
      color: var(--text-secondary);
      text-align: right;
      white-space: nowrap;
    }
    .work-send-en {
      appearance: none;
      min-height: 22px;
      padding: 0 1px;
      background:
        linear-gradient(180deg, rgba(255, 255, 255, 0.014) 0%, transparent 48%),
        var(--material-readout);
      border: 1px solid #2A2B25;
      border-radius: 1px;
      color: #73766C;
      font-family: var(--font-num);
      font-size: 7.5px;
      font-weight: 800;
      cursor: pointer;
      box-shadow:
        inset 0 1px 2px rgba(0, 0, 0, 0.54),
        0 1px 0 rgba(255, 255, 255, 0.025);
      transition: border-color 0.1s, color 0.1s, background 0.1s, box-shadow 0.1s;
    }
    .work-send-en:hover {
      border-color: #4C4E44;
      color: var(--text-secondary);
    }
    .work-send-en.on {
      background:
        linear-gradient(180deg, rgba(134, 239, 172, 0.08) 0%, transparent 52%),
        linear-gradient(180deg, #173022 0%, #102218 100%);
      border-color: #35694A;
      color: #B7EFCB;
      box-shadow:
        inset 0 1px 2px rgba(0, 0, 0, 0.42),
        inset 0 -1px 0 rgba(34, 197, 94, 0.3);
    }
    .work-master-meta {
      display: flex;
      flex-direction: column;
      gap: 4px;
      min-height: 0;
    }

    /* Fine range controls used inside the selected-channel workspace.
       Their material is shared with the precision EQ sliders above. */

    /* Responsive console composition */
    @media (max-width: 1100px) {
      .work-meta {
        display: none;
      }
    }

    @media (max-width: 920px) {
      .work-body {
        grid-template-columns: 1fr 1fr;
        grid-auto-rows: minmax(150px, 1fr);
      }
    }

    /* On a large console display, give the analyser a full-width lower bay
       instead of stretching four short modules into excessive empty height. */
    @media (min-width: 1500px) and (min-height: 960px) {
      .work-body {
        grid-template-columns: minmax(240px, 0.86fr) minmax(280px, 1.02fr) minmax(300px, 1.12fr);
        grid-template-rows: minmax(190px, 0.9fr) minmax(0, 1fr);
      }
      .work-channel {
        grid-column: 1;
        grid-row: 1;
      }
      .work-processing {
        grid-column: 2;
        grid-row: 1;
      }
      .work-routing {
        grid-column: 3;
        grid-row: 1;
      }
      .work-eq {
        grid-column: 1 / -1;
        grid-row: 2;
      }
    }

    /* Keep the complete deck and work area reachable in compact or short
       viewports. The workspace is the single vertical scroller here, so
       controls never get stranded inside a clipped mixer viewport. */
    @media (max-width: 900px), (max-height: 600px) {
      .mixer-view {
        flex: 0 0 auto;
        height: auto;
        min-height: 100%;
        overflow: visible;
      }
      .mixer-workspace {
        flex: 0 0 auto;
        overflow: visible;
      }
      .work-body {
        flex: 0 0 auto;
        overflow: visible;
      }
    }

    @media (max-width: 900px) {
      /* Mobile mixer metrics: bounded non-shrinking width */
      :root {
        --strip-w: 122px;
        --strip-w-master: 130px;
        --strip-gap: 5px;
        --mixer-rail-pad: 6px;
      }

      .top-bar #btnAddChannel,
      .selected-context-badge {
        display: none;
      }

      .nav-tab {
        min-height: 44px;
      }

      /* Channel Selector Strip: Horizontally scrollable row, non-wrapping */
      .channel-selector-strip {
        min-height: 46px;
        padding: 4px 8px;
        gap: 5px;
        overflow-x: auto;
        overflow-y: hidden;
        white-space: nowrap;
        -webkit-overflow-scrolling: touch;
        scrollbar-width: none;
      }
      .channel-selector-strip::-webkit-scrollbar {
        display: none;
      }
      .ch-pill {
        min-height: 38px;
        padding: 0 10px;
        font-size: 11px;
        flex: 0 0 auto;
        touch-action: manipulation;
      }

      /* Mixer View Container */
      .mixer-view {
        flex: 0 0 auto;
        width: 100%;
        height: auto;
        min-height: 100%;
        display: flex;
        flex-direction: column;
        overflow: visible;
        background: var(--material-chassis);
      }

      /* Upper Console Deck: Horizontal touch-scrollable rail */
      .mixer-deck {
        display: flex;
        flex-direction: row;
        align-items: stretch;
        width: 100%;
        height: 490px;
        min-height: 490px;
        max-height: 490px;
        flex: 0 0 490px;
        overflow-x: auto;
        overflow-y: hidden;
        overscroll-behavior-x: contain;
        scrollbar-width: thin;
        scrollbar-color: #3E4038 #0A0B09;
        -webkit-overflow-scrolling: touch;
        background: var(--material-chassis);
        border-bottom: 1px solid #2A2B25;
        box-shadow:
          inset 0 1px 0 rgba(255, 255, 255, 0.045),
          inset 0 -1px 0 rgba(0, 0, 0, 0.65),
          0 3px 10px rgba(0, 0, 0, 0.34);
      }
      .mixer-deck::-webkit-scrollbar {
        height: 5px;
      }
      .mixer-deck::-webkit-scrollbar-track {
        background: #0A0B09;
      }
      .mixer-deck::-webkit-scrollbar-thumb {
        background: #3E4038;
        border-radius: 3px;
      }

      /* Input and bus channel bank: expands to fit strips horizontally */
      .mixer-bank {
        flex: 0 0 auto;
        display: flex;
        align-items: stretch;
        width: max-content;
        max-width: none;
        min-width: 0;
        padding: var(--mixer-rail-pad);
        overflow: visible;
        scrollbar-width: none;
        background:
          linear-gradient(180deg, rgba(0, 0, 0, 0.32) 0%, transparent 10%, transparent 88%, rgba(0, 0, 0, 0.3) 100%),
          linear-gradient(90deg, rgba(0, 0, 0, 0.16) 0%, transparent 5%, transparent 95%, rgba(0, 0, 0, 0.18) 100%),
          var(--brushed-grain),
          linear-gradient(180deg, #151611 0%, #11120E 100%);
      }
      .mixer-bank::-webkit-scrollbar {
        display: none;
      }

      .strip-rail {
        display: flex;
        align-items: stretch;
        gap: var(--strip-gap);
        min-width: min-content;
      }

      /* Master Bay: Appended seamlessly at the end of the scrollable deck */
      .mixer-master-bay {
        flex: 0 0 auto;
        display: flex;
        align-items: stretch;
        padding: var(--mixer-rail-pad);
        padding-left: 6px;
        padding-right: 12px;
        background: var(--material-master-bay);
        border-left: 1px solid #595345;
        box-shadow:
          -5px 0 16px rgba(0, 0, 0, 0.5),
          inset 1px 0 0 rgba(255, 255, 255, 0.065);
        position: static;
        z-index: 1;
      }

      /* Dedicated Mobile Channel Strip: Pure flex column with bounded width */
      .channel-strip {
        display: flex;
        flex-direction: column;
        flex: 0 0 var(--strip-w);
        width: var(--strip-w);
        min-width: var(--strip-w);
        max-width: var(--strip-w);
        flex-shrink: 0;
        height: 100%;
        padding: 6px 5px;
        gap: 4px;
        box-sizing: border-box;
        overflow: visible;
        background: var(--material-strip);
        border: 1px solid #373831;
        border-radius: var(--radius-sm);
        box-shadow:
          inset 0 1px 0 rgba(255, 255, 255, 0.10),
          inset 1px 0 0 rgba(255, 255, 255, 0.025),
          inset -1px 0 0 rgba(0, 0, 0, 0.35),
          inset 0 -1px 0 rgba(0, 0, 0, 0.58),
          0 3px 8px rgba(0, 0, 0, 0.32);
      }
      .channel-strip.master-strip {
        flex: 0 0 var(--strip-w-master);
        width: var(--strip-w-master);
        min-width: var(--strip-w-master);
        max-width: var(--strip-w-master);
        background: var(--material-master);
        border-color: #514C3E;
      }

      /* Strip Head */
      .strip-head {
        width: 100%;
        min-height: 34px;
        padding: 3px 4px;
        gap: 1px;
        flex-shrink: 0;
      }
      .strip-head-row {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 2px;
      }
      .strip-num-badge {
        font-size: 11px;
        line-height: 1;
      }
      .strip-format-tag {
        font-size: 7.5px;
        padding: 1px 2px;
      }
      .strip-name {
        font-size: 10.5px;
        line-height: 1.2;
      }

      /* Strip Source Row */
      .strip-source-row {
        min-height: 20px;
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 3px;
        flex-shrink: 0;
      }
      .strip-source-label {
        appearance: none;
        background: none;
        border: 0;
        padding: 0;
        text-align: left;
        color: var(--text-secondary);
        font-family: var(--font-num);
        font-size: 8px;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
        flex: 1 1 auto;
        min-width: 0;
      }
      .strip-source-actions {
        display: flex;
        gap: 2px;
        flex: 0 0 auto;
      }
      .strip-source-action {
        min-height: 20px;
        padding: 0 3px;
        font-size: 7px;
      }

      /* Processing Badges (2 rows of 3) */
      .strip-badges {
        display: grid;
        grid-template-columns: repeat(3, minmax(0, 1fr));
        gap: 2px;
        flex-shrink: 0;
      }
      .proc-badge {
        min-height: 18px;
        padding: 1px;
        font-size: 7.5px;
        line-height: 1;
      }

      /* Fader + Meter Well: Clean side-by-side flexbox */
      .fader-meter-bay {
        display: flex;
        flex-direction: row;
        align-items: stretch;
        gap: 3px;
        flex: 1 1 auto;
        min-height: 130px;
        padding: 5px 3px;
        background: var(--material-well);
        border: 1px solid #1D1E19;
        border-radius: var(--radius-xs);
        box-shadow:
          inset 0 3px 8px rgba(0, 0, 0, 0.78),
          inset 0 0 0 1px rgba(255, 255, 255, 0.018),
          0 1px 0 rgba(255, 255, 255, 0.035);
      }

      /* Fader Track */
      .v-fader-track {
        position: relative;
        flex: 1 1 auto;
        min-width: 44px;
        height: auto;
        min-height: 100px;
        display: flex;
        justify-content: center;
        align-items: stretch;
        margin: 0;
        order: 0;
      }
      .v-fader-scale {
        display: block;
        left: 1px;
        width: 14px;
        font-size: 7px;
      }
      .v-fader-slot {
        width: 6px;
      }
      .v-fader-cap {
        width: 36px;
        height: 22px;
      }

      /* Meter Column */
      .meter-column {
        display: flex;
        flex-direction: column;
        flex: 0 0 38px;
        width: 38px;
        gap: 2px;
        min-height: 0;
        align-self: stretch;
        margin: 0;
        order: initial;
      }
      .clip-led {
        height: 11px;
        font-size: 7px;
        line-height: 10px;
        margin: 0;
      }
      .strip-meter-canvas {
        flex: 1 1 0;
        width: 100%;
        min-height: 60px;
        height: auto;
        margin: 0;
      }
      .strip-gr-meter {
        flex: 0 0 19px;
        min-height: 19px;
        font-size: 6.5px;
        margin: 0;
      }

      /* Rotary Knobs on Channel Strip (Gain inside meter-column, Pan below readout) */
      .strip-gain-control {
        flex: 0 0 auto;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        gap: 1px;
        min-height: 0;
      }
      .strip-pan-control {
        flex: 0 0 auto;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        gap: 1px;
        min-height: 0;
      }
      .strip-knob {
        width: 32px;
        height: 32px;
      }
      .strip-knob .rotary-pointer {
        top: 2px;
        height: 9px;
        transform-origin: 50% 9px;
      }
      .strip-knob-label {
        color: var(--text-secondary);
        font-size: 7px;
        font-weight: 800;
        letter-spacing: 0.3px;
        text-transform: uppercase;
        white-space: nowrap;
        line-height: 1;
      }
      .strip-knob-value {
        color: var(--text-primary);
        font-family: var(--font-num);
        font-size: 8px;
        font-weight: 700;
        white-space: nowrap;
        line-height: 1;
      }

      /* Readout */
      .strip-db-readout {
        min-height: 22px;
        padding: 2px 3px;
        font-size: 11px;
        flex-shrink: 0;
      }

      /* Buttons */
      .strip-btns {
        display: grid;
        grid-template-columns: repeat(2, minmax(0, 1fr));
        gap: 3px;
        flex-shrink: 0;
      }
      .strip-btns .hw-btn {
        min-height: 34px;
        padding: 0 2px;
        font-size: 9px;
      }
      .strip-select-btn {
        grid-column: 1 / -1;
        min-height: 30px;
        display: inline-flex;
      }
      .strip-master-solo {
        display: inline-flex;
      }
      .master-strip .strip-select-btn {
        grid-column: auto;
      }
      .master-strip .strip-limiter-btn {
        display: inline-flex;
      }
      .strip-add-btn {
        grid-column: 1 / -1;
        display: inline-flex;
        min-height: 28px;
      }

      /* Detail Workspace Below Deck */
      .mixer-workspace {
        flex: 0 0 auto;
        overflow: visible;
        border-top: 1px solid #282923;
      }
      .work-head {
        min-height: 40px;
        padding: 4px 8px;
        gap: 6px;
      }
      .work-name {
        max-width: 140px;
      }
      .work-actions .hw-btn {
        min-height: 34px;
        padding: 0 8px;
        font-size: 9.5px;
      }
      .work-body {
        flex: 0 0 auto;
        overflow: visible;
        display: grid;
        grid-template-columns: 1fr;
        grid-auto-rows: max-content;
        gap: 8px;
        padding: 8px 8px calc(28px + env(safe-area-inset-bottom, 0px));
      }
      .work-module {
        padding: 8px;
        overflow: visible;
      }
      .work-eq-wrap {
        min-height: 140px;
      }
      .work-send-row {
        min-height: 32px;
      }
      .work-send-en {
        min-height: 28px;
      }
      .work-toggle {
        min-height: 28px;
      }
      .work-route-btn {
        min-height: 30px;
      }
      .work-insert-btn {
        min-height: 32px;
      }
      .work-link {
        min-height: 26px;
      }
    }

    @media (max-width: 600px) {
      .work-name {
        max-width: 110px;
      }
      .work-actions .hw-btn {
        padding: 0 6px;
        font-size: 9px;
      }
    }

    @media (max-width: 360px) {
      :root {
        --strip-w: 114px;
        --strip-w-master: 122px;
        --strip-gap: 4px;
      }
      .mixer-bank {
        padding: 5px;
      }
      .mixer-master-bay {
        padding: 5px 5px 5px 3px;
      }
      .strip-btns .hw-btn {
        font-size: 8.5px;
      }
      .v-fader-scale {
        display: none;
      }
      .meter-column {
        flex: 0 0 34px;
        width: 34px;
      }
      .v-fader-cap {
        width: 32px;
      }
      .strip-knob {
        width: 28px;
        height: 28px;
      }
      .work-head {
        padding: 4px 6px;
      }
      .work-name {
        max-width: 85px;
      }
      .work-actions .hw-btn {
        min-height: 32px;
        padding: 0 5px;
        font-size: 8.5px;
      }
    }

    /* Desktop console: keep the real strip bank compact when it fits, and use
       the existing selected-channel workspace to occupy the remaining bay. */
    @media (min-width: 901px) {
      :root {
        --strip-w: 144px;
        --strip-w-master: 160px;
        --strip-gap: 6px;
      }

      .console-shell {
        grid-template-rows: 44px minmax(0, 1fr) 60px;
      }

      .top-bar {
        height: 44px;
        min-height: 44px;
        flex-wrap: nowrap;
      }

      .channel-selector-strip {
        display: none;
      }

      .main-nav {
        min-height: 60px;
        display: flex;
        justify-content: center;
        gap: 8px;
        padding: 6px 16px;
      }

      .nav-tab {
        flex: 1 1 260px;
        min-width: 0;
        max-width: 260px;
        min-height: 48px;
        border-radius: 1px;
        font-size: 15px;
        letter-spacing: 0.6px;
      }

      #btnAddChannel {
        display: none;
      }

      .audio-setup-shortcut {
        display: inline-flex;
      }

      .mixer-view {
        flex: 1 1 auto;
        height: 100%;
        min-height: 0;
        overflow: hidden;
      }

      .mixer-deck {
        flex: 0 1 auto;
        height: clamp(560px, 80vh, 860px);
        max-height: calc(100% - 16px);
        min-height: 0;
        margin: 8px 0 0;
      }

      .mixer-bank {
        flex: 1 1 auto;
        width: auto;
        max-width: calc(100% - var(--strip-w-master) - 28px);
        padding: 0 8px;
      }

      .mixer-deck.is-content-sized {
        width: max-content;
        max-width: 100%;
      }

      .mixer-deck.is-content-sized .mixer-bank {
        flex: 0 1 auto;
        width: max-content;
        max-width: none;
      }

      .mixer-view.has-workspace-rail {
        flex-direction: row;
        gap: 8px;
      }

      .mixer-view.has-workspace-rail .mixer-deck {
        flex: 0 0 auto;
        height: clamp(560px, 80vh, 860px);
        max-height: calc(100% - 16px);
        min-height: 0;
        margin: 8px 0;
      }

      .mixer-view.has-workspace-rail .mixer-workspace {
        flex: 1 1 0;
        min-width: 280px;
        height: clamp(560px, 80vh, 860px);
        max-height: calc(100% - 16px);
        margin: 8px 0;
        display: flex;
        border-left: 1px solid #363730;
      }

      .mixer-view.has-workspace-rail .work-body,
      .mixer-view.has-workspace-under .work-body {
        grid-template-columns: repeat(auto-fit, minmax(min(100%, 220px), 1fr));
        grid-auto-rows: minmax(0, 1fr);
      }

      .mixer-view.has-narrow-workspace .work-meta {
        display: none;
      }

      .mixer-view.has-narrow-workspace .work-ident {
        flex: 1 1 auto;
        min-width: 0;
      }

      .mixer-view.has-narrow-workspace .work-name {
        max-width: 130px;
      }

      .mixer-view.has-workspace-under {
        flex-direction: column;
      }

      .mixer-view.has-workspace-under .mixer-deck {
        flex: 0 0 auto;
        height: clamp(400px, 58vh, 680px);
        max-height: calc(100% - 220px);
        min-height: 360px;
        margin: 8px 0;
      }

      .mixer-view.has-workspace-under .mixer-workspace {
        flex: 1 1 0;
        min-height: 200px;
        display: flex;
      }

      .mixer-view.has-dense-bank {
        flex-direction: column;
      }

      .mixer-view.has-dense-bank .mixer-deck {
        flex: 0 1 auto;
        height: clamp(560px, 80vh, 860px);
        max-height: calc(100% - 16px);
        min-height: 0;
        margin: 8px 0;
      }

      .mixer-view.has-dense-bank .mixer-workspace {
        display: none;
      }

      .mixer-master-bay {
        padding: 0 8px 0 6px;
      }

      .channel-strip {
        padding: 6px;
        gap: 5px;
      }

      .strip-source-row {
        min-width: 0;
        min-height: 24px;
        display: flex;
        align-items: center;
        gap: 4px;
      }

      .strip-source-label {
        appearance: none;
        background: none;
        border: 0;
        padding: 0;
        text-align: left;
        cursor: pointer;
      }

      .strip-source-label {
        min-width: 0;
        flex: 1 1 auto;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
        color: var(--text-secondary);
        font-size: 9px;
        font-family: var(--font-num);
      }

      .strip-source-actions {
        display: flex;
        gap: 3px;
        flex: 0 0 auto;
      }

      .strip-source-action {
        min-height: 21px;
        padding: 0 3px;
        font-size: 7px;
      }

      .fader-meter-bay {
        flex-direction: row;
        gap: 4px;
        min-height: 130px;
        padding: 7px 5px;
      }

      .meter-column {
        order: initial;
        width: 42px;
        flex: 0 0 42px;
        height: auto;
        display: flex;
        flex-direction: column;
        gap: 3px;
      }

      .clip-led {
        grid-column: auto;
      }

      .strip-meter-canvas {
        grid-column: auto;
        grid-row: auto;
        min-height: 100px;
        height: auto;
      }

      .strip-gr-meter {
        grid-column: auto;
        grid-row: auto;
      }

      .strip-gain-control,
      .strip-pan-control {
        min-width: 0;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        gap: 2px;
      }

      .strip-gain-control {
        flex: 0 0 auto;
      }

      .strip-pan-control {
        flex: 0 0 auto;
        min-height: 64px;
      }

      .strip-knob {
        width: 40px;
        height: 40px;
      }

      .strip-knob .rotary-pointer {
        top: 3px;
        height: 12px;
        transform-origin: 50% 12px;
      }

      .strip-knob-label {
        color: var(--text-secondary);
        font-size: 8px;
        font-weight: 800;
        letter-spacing: 0.4px;
        text-transform: uppercase;
        white-space: nowrap;
      }

      .strip-knob-value {
        color: var(--text-primary);
        font-family: var(--font-num);
        font-size: 9px;
        font-weight: 700;
        white-space: nowrap;
      }

      .strip-btns {
        grid-template-columns: repeat(2, minmax(0, 1fr));
        gap: 5px;
      }

      .strip-btns .hw-btn {
        min-height: 40px;
        padding: 0 4px;
        font-size: 10px;
      }

      .strip-select-btn {
        grid-column: 1 / -1;
        display: inline-flex;
      }

      .strip-master-solo {
        display: inline-flex;
      }

      .master-strip .strip-select-btn {
        grid-column: auto;
      }

      .strip-add-btn {
        grid-column: 1 / -1;
        display: inline-flex;
      }
    }

    @media (min-width: 901px) and (max-width: 1000px) {
      .audio-setup-shortcut {
        display: none;
      }
      .channel-strip {
        padding: 6px;
        gap: 5px;
      }
      .meter-column {
        width: 42px;
        flex-basis: 42px;
        height: auto;
        grid-template-columns: none;
      }
      .strip-knob {
        width: 42px;
        height: 42px;
      }
    }

    /* Shared Screen Grids & Toolbars (Views 1..6) */
    .screen-pad {
      padding: 12px;
      display: flex;
      flex-direction: column;
      gap: 12px;
      flex: 1;
    }

    .screen-toolbar {
      background: var(--material-panel);
      border: 1px solid #34352F;
      border-radius: var(--radius-sm);
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.05),
        inset 0 -1px 0 rgba(0, 0, 0, 0.42),
        0 2px 6px rgba(0, 0, 0, 0.26);
      padding: 8px 12px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      flex-wrap: wrap;
      gap: 10px;
    }

    .toolbar-group {
      display: flex;
      align-items: center;
      gap: 8px;
      flex-wrap: wrap;
    }

    .panel-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
      gap: 12px;
    }

    .hw-panel {
      background: var(--material-panel);
      border: 1px solid #34352F;
      border-radius: var(--radius-sm);
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.05),
        inset 0 -1px 0 rgba(0, 0, 0, 0.42),
        0 2px 6px rgba(0, 0, 0, 0.24);
      padding: 12px;
      display: flex;
      flex-direction: column;
      gap: 12px;
    }

    .hw-panel-head {
      display: flex;
      align-items: center;
      justify-content: space-between;
      flex-wrap: wrap;
      gap: 8px;
      padding-bottom: 8px;
      border-bottom: 1px solid var(--border-subtle);
    }
    .hw-panel-head > div {
      flex-wrap: wrap;
    }

    .hw-panel-title {
      font-size: 12px;
      font-weight: 800;
      letter-spacing: 0.8px;
      text-transform: uppercase;
      color: var(--text-secondary);
    }

    .param-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(156px, 1fr));
      gap: 8px;
    }

    .param-cell {
      background:
        radial-gradient(100% 80% at 50% 0%, rgba(255,255,255,0.022), transparent 70%),
        var(--material-readout);
      border: 1px solid #262721;
      border-radius: var(--radius-sm);
      box-shadow:
        inset 0 2px 4px rgba(0, 0, 0, 0.66),
        inset 0 0 0 1px rgba(255, 255, 255, 0.012),
        0 1px 0 rgba(255, 255, 255, 0.028);
      padding: 8px 10px;
      display: flex;
      flex-direction: column;
      gap: 6px;
    }

    .param-head {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 4px;
    }

    .param-lbl {
      font-size: 11px;
      font-weight: 700;
      color: var(--text-secondary);
      text-transform: uppercase;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    .param-val {
      font-size: 12px;
      font-weight: 700;
      font-family: var(--font-num);
      font-variant-numeric: tabular-nums;
      color: var(--text-primary);
      white-space: nowrap;
    }
    /* Keep changing numerals from shuffling the label row. */
    .param-head .param-val {
      min-width: 5em;
      text-align: right;
    }

    /* Tactile Range Slider — one reusable machined channel and cap. */
    .hw-range,
    .work-range,
    .geq-v-slider {
      -webkit-appearance: none;
      appearance: none;
      background: transparent;
      cursor: pointer;
      --range-pct: 0%;
    }
    .hw-range {
      width: 100%;
      height: var(--slider-hit-h);
      touch-action: pan-y;
    }
    .work-range {
      width: 100%;
      height: 38px;
      --slider-track-h: 5px;
      --slider-thumb-w: 18px;
      --slider-thumb-h: 22px;
      touch-action: pan-y;
    }
    .geq-v-slider {
      width: 140px;
      height: 38px;
      --slider-track-h: 5px;
      --slider-thumb-w: 16px;
      --slider-thumb-h: 20px;
      transform: rotate(-90deg);
      touch-action: pan-x;
    }
    .hw-range::-webkit-slider-runnable-track,
    .work-range::-webkit-slider-runnable-track,
    .geq-v-slider::-webkit-slider-runnable-track {
      height: var(--slider-track-h);
      background: var(--material-slider-track);
      border: 1px solid #20211C;
      border-radius: 2px;
      box-shadow:
        inset 0 2px 4px rgba(0, 0, 0, 0.88),
        inset 0 0 0 1px rgba(0, 0, 0, 0.28),
        0 1px 0 rgba(255, 255, 255, 0.035);
    }
    .hw-range::-webkit-slider-thumb,
    .work-range::-webkit-slider-thumb,
    .geq-v-slider::-webkit-slider-thumb {
      -webkit-appearance: none;
      appearance: none;
      width: var(--slider-thumb-w);
      height: var(--slider-thumb-h);
      margin-top: calc((var(--slider-track-h) - var(--slider-thumb-h)) / 2);
      background: var(--material-slider-thumb);
      border: 1px solid #11120F;
      border-radius: 2px;
      box-shadow:
        0 2px 4px rgba(0, 0, 0, 0.7),
        0 1px 0 rgba(0, 0, 0, 0.55),
        inset 0 1px 0 rgba(255, 255, 255, 0.24),
        inset 0 -1px 0 rgba(0, 0, 0, 0.7),
        0 0 0 1px rgba(255, 255, 255, 0.035);
      transition: filter 0.12s, border-color 0.12s, box-shadow 0.12s;
    }
    .hw-range:hover::-webkit-slider-thumb,
    .work-range:hover::-webkit-slider-thumb,
    .geq-v-slider:hover::-webkit-slider-thumb {
      filter: brightness(1.08);
      border-color: #30322B;
    }
    .hw-range:active::-webkit-slider-thumb,
    .hw-range.is-dragging::-webkit-slider-thumb,
    .work-range:active::-webkit-slider-thumb,
    .work-range.is-dragging::-webkit-slider-thumb,
    .geq-v-slider:active::-webkit-slider-thumb,
    .geq-v-slider.is-dragging::-webkit-slider-thumb {
      filter: brightness(1.02);
      border-color: #34362E;
      box-shadow:
        0 1px 2px rgba(0, 0, 0, 0.7),
        inset 0 1px 0 rgba(255, 255, 255, 0.18),
        inset 0 -1px 0 rgba(0, 0, 0, 0.72),
        0 0 0 1px rgba(86, 197, 214, 0.16);
    }
    .hw-range:focus-visible,
    .work-range:focus-visible,
    .geq-v-slider:focus-visible {
      outline: 2px solid var(--focus-ring);
      outline-offset: -2px;
      border-radius: var(--radius-sm);
    }
    .hw-range:disabled,
    .work-range:disabled,
    .geq-v-slider:disabled {
      cursor: not-allowed;
      opacity: 0.42;
    }
    .hw-range::-moz-range-track,
    .work-range::-moz-range-track,
    .geq-v-slider::-moz-range-track {
      height: var(--slider-track-h);
      background: var(--material-readout);
      border: 1px solid #20211C;
      border-radius: 2px;
      box-shadow: inset 0 2px 4px rgba(0, 0, 0, 0.88);
    }
    .hw-range::-moz-range-progress,
    .work-range::-moz-range-progress,
    .geq-v-slider::-moz-range-progress {
      height: var(--slider-track-h);
      background: linear-gradient(90deg, var(--slider-track-fill), var(--slider-track-fill-soft));
      border-radius: 2px;
    }
    .hw-range::-moz-range-thumb,
    .work-range::-moz-range-thumb,
    .geq-v-slider::-moz-range-thumb {
      width: var(--slider-thumb-w);
      height: var(--slider-thumb-h);
      background: var(--material-slider-thumb);
      border: 1px solid #11120F;
      border-radius: 2px;
      box-shadow:
        0 2px 4px rgba(0, 0, 0, 0.7),
        inset 0 1px 0 rgba(255, 255, 255, 0.24),
        inset 0 -1px 0 rgba(0, 0, 0, 0.7);
    }

    .hw-select {
      background: var(--material-control);
      border: 1px solid #393A33;
      border-radius: var(--radius-sm);
      box-shadow: var(--bevel-control);
      color: var(--text-primary);
      min-height: 34px;
      padding: 0 8px;
      font-size: 12px;
      font-weight: 700;
      cursor: pointer;
      width: 100%;
    }
    .param-cell .hw-select {
      padding: 0 6px;
      font-size: 11px;
    }

    .hw-input {
      background: var(--material-readout);
      border: 1px solid #34352E;
      border-radius: var(--radius-sm);
      box-shadow:
        inset 0 2px 4px rgba(0, 0, 0, 0.72),
        0 1px 0 rgba(255, 255, 255, 0.025);
      color: var(--text-primary);
      min-height: 34px;
      padding: 0 10px;
      font-size: 12px;
      font-family: var(--font-num);
      font-variant-numeric: tabular-nums;
      width: 100%;
    }

    /* EQ & Curve Graph Canvases */
    .curve-canvas-wrap {
      position: relative;
      width: 100%;
      background:
        radial-gradient(110% 80% at 50% 0%, rgba(255,255,255,0.022), transparent 72%),
        var(--material-readout);
      border: 1px solid #20211C;
      border-radius: var(--radius-sm);
      overflow: hidden;
      box-shadow:
        inset 0 3px 8px rgba(0, 0, 0, 0.82),
        inset 0 0 0 1px rgba(255, 255, 255, 0.012),
        0 1px 0 rgba(255, 255, 255, 0.03);
    }
    .eq-curve-canvas {
      width: 100%;
      height: 280px;
      display: block;
      cursor: crosshair;
      touch-action: none;
    }
    .dyn-curve-canvas {
      width: 100%;
      height: 220px;
      display: block;
    }

    /* 31-Band GEQ Bank */
    .geq-faders-bank {
      display: grid;
      grid-template-columns: repeat(31, minmax(38px, 1fr));
      gap: 3px;
      background:
        linear-gradient(180deg, rgba(0, 0, 0, 0.32) 0%, transparent 12%, transparent 88%, rgba(0, 0, 0, 0.2) 100%),
        var(--brushed-grain),
        linear-gradient(180deg, #10110D 0%, #0A0B09 100%);
      border: 1px solid #23241E;
      border-radius: var(--radius-sm);
      padding: 10px 4px;
      overflow-x: auto;
      min-height: 220px;
      box-shadow:
        inset 0 3px 8px rgba(0, 0, 0, 0.5),
        inset 0 -1px 0 rgba(255, 255, 255, 0.028);
    }
    .geq-col {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 6px;
      min-width: 38px;
    }
    .geq-v-track {
      height: 140px;
      display: flex;
      justify-content: center;
      align-items: center;
    }

    /* FX Slots Grid */
    .fx-slots-strip {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(130px, 1fr));
      gap: 8px;
    }
    .fx-slot-card {
      background:
        linear-gradient(180deg, rgba(255, 255, 255, 0.018) 0%, transparent 50%),
        var(--material-readout);
      border: 1px solid #292A24;
      border-radius: var(--radius-sm);
      box-shadow:
        inset 0 2px 4px rgba(0, 0, 0, 0.66),
        0 1px 0 rgba(255, 255, 255, 0.028);
      padding: 8px;
      display: flex;
      flex-direction: column;
      gap: 6px;
      cursor: pointer;
    }
    .fx-slot-card.active {
      border-color: var(--selected-border);
      box-shadow: var(--bevel-selected);
    }
    .fx-slot-card.bypassed {
      opacity: 0.55;
    }

    /* Routing Matrix Table */
    .matrix-scroll {
      width: 100%;
      overflow-x: auto;
      border: 1px solid var(--border);
      border-radius: var(--radius-sm);
      background: var(--recessed);
      box-shadow: var(--bevel-recessed);
    }
    .routing-table {
      width: 100%;
      border-collapse: collapse;
      font-size: 12px;
      min-width: 600px;
    }
    .routing-table th, .routing-table td {
      border: 1px solid var(--border-subtle);
      padding: 6px 8px;
      text-align: center;
      white-space: nowrap;
    }
    .routing-table th {
      background: var(--grad-panel);
      font-weight: 800;
      color: var(--text-secondary);
      font-size: 11px;
      letter-spacing: 0.5px;
    }
    .xpoint-btn {
      width: 34px;
      height: 30px;
      background: var(--control);
      border: 1px solid var(--border-subtle);
      border-radius: 2px;
      color: var(--text-muted);
      font-family: var(--font-num);
      font-weight: 800;
      cursor: pointer;
    }
    .xpoint-btn.patched {
      background: #173722;
      border-color: var(--sig-green);
      color: #86EFAC;
      box-shadow: 0 0 4px rgba(34, 197, 94, 0.45);
    }

    /* Modal Plugin / PatchStorage Browser */
    .modal-backdrop {
      position: fixed;
      inset: 0;
      background: var(--modal-backdrop);
      display: none;
      align-items: center;
      justify-content: center;
      padding: 16px;
      z-index: 100;
    }
    .modal-backdrop.open {
      display: flex;
    }
    .modal-dialog {
      width: 100%;
      max-width: 860px;
      max-height: 85vh;
      background: var(--material-panel);
      border: 1px solid #3A3B33;
      border-radius: var(--radius-sm);
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.07),
        0 16px 40px rgba(0, 0, 0, 0.85);
      display: flex;
      flex-direction: column;
      overflow: hidden;
    }
    .modal-header {
      padding: 12px 16px;
      border-bottom: 1px solid #20211B;
      background: var(--material-chassis);
      box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.055);
      display: flex;
      flex-direction: column;
      gap: 10px;
    }
    .modal-tabs {
      display: flex;
      gap: 6px;
      overflow-x: auto;
      scrollbar-width: thin;
    }
    .modal-tab {
      min-height: 32px;
      padding: 0 12px;
      font-size: 11px;
      font-weight: 700;
    }
    .modal-body {
      padding: 14px 16px;
      overflow-y: auto;
      flex: 1;
    }
    .modal-footer {
      padding: 10px 16px;
      border-top: 1px solid #20211B;
      background: var(--material-chassis);
      box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.045);
      display: flex;
      align-items: center;
      justify-content: space-between;
    }
    .catalog-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
      gap: 10px;
    }
    .catalog-card {
      background:
        linear-gradient(180deg, rgba(255, 255, 255, 0.018) 0%, transparent 52%),
        var(--material-readout);
      border: 1px solid #292A24;
      border-radius: var(--radius-sm);
      box-shadow:
        inset 0 2px 4px rgba(0, 0, 0, 0.66),
        0 1px 0 rgba(255, 255, 255, 0.028);
      padding: 10px;
      display: flex;
      flex-direction: column;
      justify-content: space-between;
      gap: 8px;
    }
    .catalog-desc {
      font-size: 11px;
      color: var(--text-secondary);
      line-height: 1.35;
    }

      .aux-master-grid {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
        gap: 8px;
      }
      .aux-master-card {
        min-width: 0;
        display: flex;
        flex-direction: column;
        gap: 8px;
        padding: 8px;
        background: var(--material-strip);
        border: 1px solid #373831;
        border-radius: var(--radius-xs);
        box-shadow: var(--bevel-raised);
      }
      .aux-master-card.selected {
        border-color: rgba(86, 197, 214, 0.58);
        box-shadow: var(--bevel-selected);
      }
      .aux-master-head,
      .aux-master-actions {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 6px;
      }
      .aux-master-head strong {
        min-width: 0;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
        color: var(--text-primary);
      }
      .aux-master-meter {
        display: block;
        width: 100%;
        height: 54px;
        background: var(--material-readout);
        border: 1px solid #292A24;
      }
      .aux-master-control {
        display: grid;
        grid-template-columns: 62px minmax(0, 1fr) 54px;
        align-items: center;
        gap: 6px;
      }
      .aux-master-control .param-lbl {
        font-size: 9px;
      }
      .aux-master-control .param-val {
        font-size: 9px;
        text-align: right;
        white-space: nowrap;
      }
      .aux-master-control .hw-range {
        min-width: 0;
      }

      .channel-dialog-backdrop {
        z-index: 120;
        overflow-y: auto;
        padding:
          max(12px, env(safe-area-inset-top))
          max(12px, env(safe-area-inset-right))
          max(12px, env(safe-area-inset-bottom))
          max(12px, env(safe-area-inset-left));
      }
      .channel-dialog {
        width: min(100%, 440px);
        max-width: 440px;
        max-height: min(82vh, 520px);
        border-color: #4A4B42;
      }
      .channel-dialog-header {
        display: block;
        padding: 14px 16px 10px;
      }
      .channel-dialog-title {
        display: block;
        color: var(--text-primary);
        font-size: 13px;
        font-weight: 800;
        letter-spacing: 0.8px;
      }
      .channel-dialog-context {
        display: block;
        margin-top: 5px;
        color: var(--text-secondary);
        font-size: 10px;
        font-family: var(--font-num);
        letter-spacing: 0.35px;
      }
      .channel-dialog-form {
        display: flex;
        flex: 1 1 auto;
        flex-direction: column;
        min-height: 0;
      }
      .channel-dialog-body {
        display: flex;
        flex: 1 1 auto;
        flex-direction: column;
        gap: 12px;
        min-height: 0;
        overflow-y: auto;
        padding: 14px 16px;
      }
      .channel-dialog-field {
        display: flex;
        flex-direction: column;
        gap: 5px;
      }
      .channel-dialog-field .param-lbl {
        color: var(--text-secondary);
      }
      .channel-dialog-note,
      .channel-dialog-warning {
        color: var(--text-secondary);
        font-size: 11px;
        line-height: 1.45;
      }
      .channel-dialog-warning {
        padding: 10px;
        background: var(--material-readout);
        border-left: 2px solid var(--accent-cyan);
      }
      .channel-dialog-error {
        margin: 0 16px;
        padding: 8px 10px;
        color: #FCA5A5;
        background: rgba(92, 24, 24, 0.26);
        border: 1px solid rgba(184, 68, 68, 0.48);
        border-radius: var(--radius-xs);
        font-size: 11px;
        line-height: 1.35;
      }
      .channel-dialog-error[hidden] {
        display: none;
      }
      .channel-dialog-footer {
        flex: 0 0 auto;
        justify-content: flex-end;
        gap: 8px;
      }
      .channel-dialog-footer .hw-btn {
        min-width: 88px;
        min-height: 40px;
      }
      .channel-dialog :focus-visible {
        outline: 2px solid var(--selected-border);
        outline-offset: 2px;
      }
      body.channel-dialog-open {
        overflow: hidden;
        overscroll-behavior: none;
      }

      @media (max-width: 720px) {
        .channel-dialog {
          width: 100%;
          max-height: calc(100dvh - env(safe-area-inset-top) - env(safe-area-inset-bottom) - 24px);
        }
        .channel-dialog-body {
          overscroll-behavior: contain;
        }
        .channel-dialog-footer .hw-btn {
          min-width: 96px;
          min-height: 44px;
        }
      }

    /* RESPONSIVE BREAKPOINTS (Desktop, Tablet, Mobile) */
    @media (max-width: 1024px) {
      .panel-grid {
        grid-template-columns: 1fr;
      }
    }

    @media (max-width: 768px) {
      .top-bar {
        padding: 0 8px;
        flex-wrap: nowrap;
        overflow: hidden;
      }
      .hide-xs {
        display: none !important;
      }
    }

    @media (max-width: 640px) {
      .top-bar {
        padding: 0 6px;
        height: 44px;
        min-height: 44px;
        flex-wrap: nowrap;
        overflow: hidden;
      }
      .telemetry-cluster {
        flex-wrap: nowrap;
        gap: 6px;
      }
      #btnAddChannel {
        display: none;
      }
      .selected-context-badge {
        display: none;
      }
      .console-instrument-pod {
        gap: 6px;
        padding: 3px 6px;
        font-size: 11px;
      }
      .main-nav {
        display: grid;
        grid-template-columns: repeat(7, minmax(0, 1fr));
        overflow: visible;
        padding: 4px max(3px, env(safe-area-inset-right, 0px)) calc(4px + env(safe-area-inset-bottom, 0px)) max(3px, env(safe-area-inset-left, 0px));
        gap: 3px;
      }
      .nav-tab {
        min-width: 0;
        min-height: 44px;
        padding: 0 1px;
        font-size: clamp(7.5px, 2.2vw, 10.5px);
        letter-spacing: 0;
      }
    }

    @media (max-width: 339px) {
      /* At the narrowest phones the full labels cannot fit seven abreast.
         Keep a readable, docked horizontal rail instead of truncating them. */
      .main-nav {
        display: flex;
        overflow-x: auto;
        gap: 3px;
        scroll-snap-type: x proximity;
        scrollbar-width: none;
      }
      .main-nav::-webkit-scrollbar {
        display: none;
      }
      .nav-tab {
        flex: 0 0 64px;
        min-width: 64px;
        padding: 0 4px;
        font-size: 9.5px;
        scroll-snap-align: center;
      }
    }

    @media (max-width: 360px) {
      .brand-mark {
        font-size: 11.5px;
        letter-spacing: 0.8px;
        padding: 3px 5px;
      }
      .top-bar .hw-btn {
        font-size: 10px;
        padding: 0 6px;
        min-height: 30px;
      }
      .console-instrument-pod {
        font-size: 10px;
        padding: 2px 4px;
      }
    }

    /* Static material treatments respect reduced-motion without disabling feedback. */
    @media (prefers-reduced-motion: reduce) {
      .hw-btn,
      .nav-tab,
      .ch-pill,
      .channel-strip,
      .strip-head,
      .rotary-dial,
      .work-toggle,
      .work-route-btn,
      .work-send-en,
      .work-insert-btn,
      .work-eq-wrap {
        transition: none;
      }
    }
    /* ==================================================================
       PRISMIX SYSTEM CONSOLE / SHARED SECURITY SURFACES
       Reuses the console's brushed-metal vocabulary rather than introducing
       a second dashboard language.
       ================================================================== */
    .system-console-grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 10px;
      align-items: start;
    }
    .system-console-grid .hw-panel {
      min-width: 0;
    }
    .system-span-2 {
      grid-column: 1 / -1;
    }
    .system-section-head {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 8px;
      margin-bottom: 8px;
      flex-wrap: wrap;
    }
    .system-section-sub {
      color: var(--text-muted);
      font-family: var(--font-num);
      font-size: 9px;
      font-weight: 800;
      letter-spacing: 0.7px;
      text-transform: uppercase;
    }
    .system-status-grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 6px;
      margin-bottom: 8px;
    }
    .system-status-cell {
      min-width: 0;
      padding: 7px 8px;
      border: 1px solid #2A2B25;
      border-radius: var(--radius-xs);
      background: var(--material-readout);
      box-shadow: inset 0 1px 3px rgba(0, 0, 0, 0.6);
    }
    .system-status-label {
      display: block;
      color: var(--text-muted);
      font-family: var(--font-num);
      font-size: 8.5px;
      font-weight: 800;
      letter-spacing: 0.55px;
      text-transform: uppercase;
      margin-bottom: 3px;
    }
    .system-status-value {
      display: block;
      min-width: 0;
      color: var(--text-primary);
      font-family: var(--font-num);
      font-size: 11.5px;
      font-weight: 800;
      font-variant-numeric: tabular-nums;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .system-status-value.mono {
      font-size: 10.5px;
      letter-spacing: 0.15px;
    }
    .system-badge {
      display: inline-flex;
      align-items: center;
      gap: 5px;
      min-height: 20px;
      padding: 1px 7px;
      border: 1px solid #3A3B33;
      border-radius: 999px;
      background: #1B1C18;
      color: var(--text-secondary);
      font-family: var(--font-num);
      font-size: 8.5px;
      font-weight: 900;
      letter-spacing: 0.55px;
      white-space: nowrap;
      text-transform: uppercase;
    }
    .system-badge::before {
      content: "";
      width: 5px;
      height: 5px;
      border-radius: 50%;
      background: #6B6E63;
      box-shadow: 0 0 4px rgba(107, 110, 99, 0.5);
    }
    .system-badge.ok { color: #B7EFCB; border-color: #35694A; background: #14271C; }
    .system-badge.ok::before { background: #34D399; box-shadow: 0 0 5px rgba(52, 211, 153, 0.65); }
    .system-badge.warn { color: #FDE68A; border-color: #6B5727; background: #2A2415; }
    .system-badge.warn::before { background: #F59E0B; box-shadow: 0 0 5px rgba(245, 158, 11, 0.6); }
    .system-badge.err { color: #FCA5A5; border-color: #6E3232; background: #2B1717; }
    .system-badge.err::before { background: #EF4444; box-shadow: 0 0 5px rgba(239, 68, 68, 0.6); }
    .system-badge.off { color: #9A9D92; }
    .system-action-row {
      display: flex;
      align-items: center;
      gap: 7px;
      flex-wrap: wrap;
      margin-top: 8px;
    }
    .system-action-row .hw-btn {
      flex: 1 1 128px;
      min-height: 38px;
    }
    .system-note {
      margin: 8px 0 0;
      color: var(--text-muted);
      font-size: 10px;
      line-height: 1.45;
    }
    .system-note strong {
      color: var(--text-secondary);
      font-weight: 800;
    }
    .system-note.warn {
      color: #D8B45E;
    }
    .system-url-box {
      display: flex;
      align-items: center;
      gap: 8px;
      padding: 7px 8px;
      border: 1px solid #2A2B25;
      border-radius: var(--radius-xs);
      background: var(--material-readout);
      box-shadow: inset 0 2px 5px rgba(0, 0, 0, 0.65);
      min-width: 0;
    }
    .system-url {
      flex: 1;
      min-width: 0;
      color: var(--accent-cyan);
      font-family: var(--font-num);
      font-size: 11.5px;
      font-weight: 800;
      line-height: 1.35;
      overflow-wrap: anywhere;
      user-select: all;
    }
    .system-web-layout {
      display: grid;
      grid-template-columns: minmax(0, 1fr) 164px;
      gap: 10px;
      align-items: start;
    }
    .system-qr-frame {
      width: 164px;
      padding: 8px;
      border: 1px solid #44463D;
      border-radius: var(--radius-xs);
      background: #F8FAF4;
      box-shadow: inset 0 0 0 1px rgba(0, 0, 0, 0.35), 0 4px 14px rgba(0, 0, 0, 0.35);
    }
    .system-qr-frame canvas {
      display: block;
      width: 100%;
      height: auto;
      image-rendering: pixelated;
    }
    .system-qr-fallback {
      display: flex;
      align-items: center;
      justify-content: center;
      aspect-ratio: 1;
      padding: 8px;
      color: #373A32;
      font-family: var(--font-num);
      font-size: 10px;
      font-weight: 800;
      text-align: center;
    }
    .system-signal {
      display: inline-flex;
      align-items: flex-end;
      gap: 2px;
      height: 13px;
      vertical-align: -2px;
    }
    .system-signal i {
      display: block;
      width: 3px;
      border-radius: 1px;
      background: #4B4E44;
    }
    .system-signal i:nth-child(1) { height: 4px; }
    .system-signal i:nth-child(2) { height: 7px; }
    .system-signal i:nth-child(3) { height: 10px; }
    .system-signal i:nth-child(4) { height: 13px; }
    .system-signal i.on { background: var(--sig-green); box-shadow: 0 0 3px rgba(34, 197, 94, 0.5); }
    .system-wifi-list {
      display: flex;
      flex-direction: column;
      gap: 5px;
    }
    .system-wifi-row {
      display: grid;
      grid-template-columns: minmax(0, 1fr) auto;
      gap: 8px;
      align-items: center;
      width: 100%;
      min-height: 46px;
      padding: 7px 9px;
      border: 1px solid #2D2E28;
      border-radius: var(--radius-xs);
      background: linear-gradient(180deg, rgba(255,255,255,0.018), transparent 48%), var(--control);
      color: var(--text-primary);
      text-align: left;
      cursor: pointer;
      box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.04), 0 1px 2px rgba(0, 0, 0, 0.4);
    }
    .system-wifi-row:hover { border-color: #4C4E44; background: var(--control-hover); }
    .system-wifi-row.active { border-color: #35694A; }
    .system-wifi-ssid {
      display: block;
      min-width: 0;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
      font-size: 12px;
      font-weight: 800;
    }
    .system-wifi-meta {
      display: flex;
      align-items: center;
      gap: 8px;
      margin-top: 2px;
      color: var(--text-muted);
      font-family: var(--font-num);
      font-size: 9px;
      font-weight: 700;
    }
    .system-modal {
      max-width: 580px;
      max-height: calc(100dvh - 24px);
    }
    .system-modal .modal-header {
      flex-wrap: wrap;
      gap: 4px;
    }
    .system-modal .modal-body {
      overflow-y: auto;
      overscroll-behavior: contain;
      -webkit-overflow-scrolling: touch;
      max-height: min(62dvh, 560px);
      padding: 14px 16px;
    }
    .system-modal .modal-footer {
      flex-wrap: wrap;
      justify-content: flex-end;
      padding-bottom: max(10px, env(safe-area-inset-bottom));
    }
    .system-dialog-context {
      color: var(--text-muted);
      font-family: var(--font-num);
      font-size: 9px;
      font-weight: 800;
      letter-spacing: 0.45px;
      text-transform: uppercase;
    }
    .system-dialog-error {
      margin: 0 16px 10px;
      padding: 8px 10px;
      border: 1px solid #6E3232;
      border-radius: var(--radius-xs);
      background: #2B1717;
      color: #FCA5A5;
      font-size: 11px;
      line-height: 1.4;
    }
    .system-dialog-error[hidden] { display: none; }
    .system-field {
      display: flex;
      flex-direction: column;
      gap: 5px;
      margin-bottom: 11px;
    }
    .system-field:last-child { margin-bottom: 0; }
    .system-field label {
      color: var(--text-secondary);
      font-family: var(--font-num);
      font-size: 9.5px;
      font-weight: 900;
      letter-spacing: 0.6px;
      text-transform: uppercase;
    }
    .system-field-hint {
      color: var(--text-muted);
      font-size: 10px;
      line-height: 1.4;
    }
    .system-pin-input {
      width: 100%;
      min-height: 46px;
      padding: 8px 11px;
      border: 1px solid #3A3C33;
      border-radius: var(--radius-xs);
      background: var(--material-readout);
      color: var(--text-primary);
      font-family: var(--font-num);
      font-size: 18px;
      font-weight: 800;
      letter-spacing: 0.35em;
      font-variant-numeric: tabular-nums;
      box-shadow: inset 0 2px 6px rgba(0, 0, 0, 0.7);
    }
    .system-pin-input:focus-visible { outline: 2px solid var(--selected-border); outline-offset: 1px; }
    .system-pin-wrap {
      position: relative;
    }
    .system-pin-wrap .system-pin-input { padding-right: 68px; }
    .system-pin-reveal {
      position: absolute;
      top: 50%;
      right: 5px;
      transform: translateY(-50%);
      min-width: 56px;
      min-height: 36px;
      border: 1px solid #3A3C33;
      border-radius: var(--radius-xs);
      background: var(--control);
      color: var(--text-secondary);
      font-family: var(--font-num);
      font-size: 8.5px;
      font-weight: 900;
      letter-spacing: 0.4px;
      cursor: pointer;
    }
    .system-input {
      width: 100%;
      min-height: 42px;
      padding: 8px 10px;
      border: 1px solid #3A3C33;
      border-radius: var(--radius-xs);
      background: var(--material-readout);
      color: var(--text-primary);
      font-family: var(--font-num);
      font-size: 12px;
      font-weight: 700;
      box-shadow: inset 0 2px 6px rgba(0, 0, 0, 0.66);
    }
    .system-input[type="password"] { letter-spacing: 0.16em; }
    .system-dialog-warning {
      padding: 9px 10px;
      border: 1px solid #6B5727;
      border-left: 3px solid #F59E0B;
      border-radius: var(--radius-xs);
      background: #262112;
      color: #E7D196;
      font-size: 11px;
      line-height: 1.5;
    }
    .system-dialog-danger {
      padding: 9px 10px;
      border: 1px solid #6E3232;
      border-left: 3px solid #EF4444;
      border-radius: var(--radius-xs);
      background: #2B1717;
      color: #F3BABA;
      font-size: 11px;
      line-height: 1.5;
    }
    .system-dialog-success {
      padding: 9px 10px;
      border: 1px solid #35694A;
      border-left: 3px solid #34D399;
      border-radius: var(--radius-xs);
      background: #14271C;
      color: #B7EFCB;
      font-size: 11px;
      line-height: 1.5;
    }
    .system-dialog-readout {
      margin-top: 9px;
      padding: 8px 10px;
      border: 1px solid #2A2B25;
      border-radius: var(--radius-xs);
      background: var(--material-readout);
      color: var(--text-secondary);
      font-family: var(--font-num);
      font-size: 10px;
      line-height: 1.45;
      overflow-wrap: anywhere;
    }
    .system-qr-modal {
      display: flex;
      justify-content: center;
      padding: 4px 0 8px;
    }
    .system-qr-modal canvas {
      width: min(100%, 320px);
      height: auto;
      image-rendering: pixelated;
      background: #fff;
      border: 8px solid #fff;
      border-radius: 2px;
      box-shadow: 0 8px 24px rgba(0, 0, 0, 0.5);
    }
    .system-toast-stack {
      position: fixed;
      left: 50%;
      bottom: calc(var(--nav-h) + max(12px, env(safe-area-inset-bottom, 0px)) + 8px);
      transform: translateX(-50%);
      z-index: 460;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 6px;
      width: min(420px, calc(100vw - 24px));
      pointer-events: none;
    }
    .system-toast {
      width: 100%;
      padding: 10px 12px;
      border: 1px solid #44463D;
      border-left: 3px solid var(--accent-cyan);
      border-radius: var(--radius-xs);
      background: var(--material-panel);
      color: var(--text-primary);
      font-size: 11px;
      font-weight: 700;
      line-height: 1.4;
      box-shadow: 0 8px 24px rgba(0, 0, 0, 0.7);
      animation: systemToastIn 140ms ease-out;
    }
    .system-toast.err { border-left-color: var(--sig-red); }
    .system-toast.ok { border-left-color: var(--sig-green); }
    @keyframes systemToastIn {
      from { opacity: 0; transform: translateY(6px); }
      to { opacity: 1; transform: translateY(0); }
    }

    /* Console authentication gate: a native System modal built from the
       shared metal surfaces, readouts, labels and tactile controls. */
    .security-login {
      position: fixed;
      inset: 0;
      z-index: 500;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 16px;
      overflow-y: auto;
      overscroll-behavior: contain;
      background: var(--modal-backdrop);
      -webkit-overflow-scrolling: touch;
    }
    .security-login[hidden] { display: none !important; }
    .modal-dialog.security-login-card {
      max-width: 440px;
      max-height: calc(100dvh - 32px);
    }
    .system-modal .modal-header.security-login-header {
      flex-direction: row;
      align-items: center;
      gap: 10px;
      padding: 12px 16px;
    }
    .security-login-body { padding-top: 16px; }
    .security-login-title {
      margin: 0;
      color: var(--text-primary);
      font-size: 14px;
      font-weight: 800;
      letter-spacing: 0.75px;
      line-height: 1.25;
      text-transform: uppercase;
    }
    .security-login-copy {
      margin: 6px 0 16px;
      color: var(--text-secondary);
      font-size: 11px;
      line-height: 1.5;
    }
    .security-login .system-dialog-error { margin: 12px 0 0; }
    .security-login-submit {
      width: 100%;
      min-height: 44px;
      margin-top: 14px;
    }
    @media (max-width: 520px) {
      .security-login {
        padding: max(16px, env(safe-area-inset-top, 0px)) 16px
                 max(16px, env(safe-area-inset-bottom, 0px));
      }
      .security-login-body { padding-top: 14px; }
    }
    /* Mixer lock: a compact native dialog over the live console. */
    .mixer-lock-gate {
      position: fixed;
      inset: 0;
      z-index: 300;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 16px;
      overflow-y: auto;
      overscroll-behavior: contain;
      background: rgba(0, 0, 0, 0.58);
      backdrop-filter: blur(2px);
      -webkit-backdrop-filter: blur(2px);
    }
    .mixer-lock-gate[hidden] { display: none !important; }
    .modal-dialog.mixer-lock-card {
      max-width: 400px;
      max-height: calc(100dvh - 32px);
    }
    .mixer-lock-card:focus { outline: none; }
    .mixer-lock-title {
      font-size: 13px;
      font-weight: 800;
      letter-spacing: 0.75px;
    }
    .mixer-lock-copy {
      margin: 0 0 14px;
      color: var(--text-secondary);
      font-size: 11px;
      line-height: 1.55;
    }
    .mixer-lock-unlock {
      width: 100%;
      min-height: 44px;
    }
    @keyframes mixerLockOverlayIn {
      from { opacity: 0; }
      to { opacity: 1; }
    }
    @keyframes mixerLockCardIn {
      from { opacity: 0; transform: translateY(5px); }
      to { opacity: 1; transform: translateY(0); }
    }
    @keyframes mixerLockOverlayOut {
      from { opacity: 1; }
      to { opacity: 0; }
    }
    @keyframes mixerLockCardOut {
      from { opacity: 1; transform: translateY(0); }
      to { opacity: 0; transform: translateY(4px); }
    }
    .mixer-lock-gate:not([hidden]) {
      animation: mixerLockOverlayIn 200ms ease-out both;
    }
    .mixer-lock-gate:not([hidden]) .mixer-lock-card {
      animation: mixerLockCardIn 200ms ease-out both;
    }
    .mixer-lock-gate.closing {
      pointer-events: none;
      animation: mixerLockOverlayOut 180ms ease-in both;
    }
    .mixer-lock-gate.closing .mixer-lock-card {
      animation: mixerLockCardOut 180ms ease-in both;
    }
    #systemModalBackdrop {
      z-index: 420;
    }
    #systemModalBackdrop .modal-dialog {
      max-height: calc(100dvh - 24px);
    }
    #systemModalBackdrop .modal-body {
      max-height: min(62dvh, 560px);
    }
    #systemModalBackdrop .modal-footer {
      position: sticky;
      bottom: 0;
      background: var(--material-panel);
      box-shadow: 0 -8px 18px rgba(0, 0, 0, 0.35);
    }
    .system-modal .modal-footer .hw-btn {
      min-height: 42px;
      min-width: 96px;
    }
    @media (max-width: 860px) {
      .system-console-grid {
        grid-template-columns: minmax(0, 1fr);
      }
      .system-span-2 {
        grid-column: auto;
      }
      .system-web-layout {
        grid-template-columns: minmax(0, 1fr);
      }
      .system-qr-frame {
        width: 144px;
        margin: 0 auto;
      }
      .system-modal .modal-body {
        max-height: min(64dvh, 520px);
      }
    }
    @media (max-width: 520px) {
      .system-status-grid {
        grid-template-columns: minmax(0, 1fr);
      }
      .system-action-row .hw-btn {
        flex-basis: 100%;
      }
      .system-modal {
        width: 100%;
        max-width: 100%;
      }
      .system-modal .modal-footer {
        justify-content: stretch;
      }
      .system-modal .modal-footer .hw-btn {
        flex: 1 1 120px;
        min-width: 0;
      }
    }
    @media (prefers-reduced-motion: reduce) {
      .system-toast { animation: none; }
      .mixer-lock-gate:not([hidden]),
      .mixer-lock-gate:not([hidden]) .mixer-lock-card,
      .mixer-lock-gate.closing,
      .mixer-lock-gate.closing .mixer-lock-card {
        animation: none;
      }
    }
  </style>
</head>
<body>
  <div class="console-shell" id="consoleShell">
    <!-- Top Hardware Status & Control Bar -->
    <header class="top-bar" role="banner">
      <div class="brand-cluster">
        <span class="brand-mark">PRISMIX</span>
        <div class="selected-context-badge" id="selectedBanner" title="Active Channel Context">
          <span class="tag">—</span>
          <span class="name">NO SELECTION</span>
        </div>
      </div>

      <div class="telemetry-cluster">
        <div class="console-instrument-pod">
          <span class="pod-item" id="engineStatePill">
            <span class="status-dot warn" id="engineStateDot"></span>
            <strong id="engineStateText">CONNECTING</strong>
          </span>
          <span class="pod-sep hide-xs"></span>
          <span class="pod-item hide-xs">
            <span class="pod-lbl">SR</span>
            <strong id="telSampleRate">—</strong>
          </span>
          <span class="pod-sep hide-xs"></span>
          <span class="pod-item hide-xs">
            <span class="pod-lbl">CPU</span>
            <strong id="telCpuLoad">—</strong>
          </span>
          <span class="pod-sep hide-xs"></span>
          <span class="pod-item hide-xs">
            <span class="pod-lbl">XRUN</span>
            <strong id="telXruns">—</strong>
          </span>
          <span class="pod-sep"></span>
          <span class="pod-item" id="netLinkPill">
            <span class="status-dot warn" id="netLinkDot"></span>
            <strong id="netLinkText">CONNECTING</strong>
          </span>
        </div>

        <button class="hw-btn" id="btnQuickAudio" type="button" disabled onclick="window.prismix.toggleAudioEngine()">CONNECTING</button>
        <button class="hw-btn audio-setup-shortcut" type="button" onclick="window.prismix.openAudioSetup()">⚙ AUDIO SETUP</button>
        <button class="hw-btn" id="btnAddChannel" type="button" disabled onclick="window.prismix.addChannel()">+ ADD CH</button>
      </div>
    </header>

    <!-- Quick Channel / Bus Selector Strip -->
    <div class="channel-selector-strip" id="channelSelectorStrip" role="navigation" aria-label="Channel Selector"></div>

    <!-- Main Dynamic Console Workspace -->
    <main class="workspace" id="mainWorkspace" role="main"></main>

    <!-- Primary 7-Screen Navigation -->
    <nav class="main-nav" id="mainNav" role="navigation" aria-label="Console Views">
      <button class="nav-tab active" data-view="0" type="button" onclick="window.prismix.setView(0)">MIXER</button>
      <button class="nav-tab" data-view="1" type="button" onclick="window.prismix.setView(1)">CHANNEL</button>
      <button class="nav-tab" data-view="2" type="button" onclick="window.prismix.setView(2)">EQ</button>
      <button class="nav-tab" data-view="3" type="button" onclick="window.prismix.setView(3)">DYNAMICS</button>
      <button class="nav-tab" data-view="4" type="button" onclick="window.prismix.setView(4)">FX</button>
      <button class="nav-tab" data-view="5" type="button" onclick="window.prismix.setView(5)">ROUTING</button>
      <button class="nav-tab" data-view="6" type="button" onclick="window.prismix.setView(6)">SYSTEM</button>
    </nav>
  </div>

  <!-- Console authentication gate: shares the System modal surface. -->
  <div class="security-login" id="securityGate" hidden>
    <section class="modal-dialog system-modal security-login-card" role="dialog" aria-modal="true"
             aria-labelledby="securityGateTitle" aria-describedby="securityGateCopy">
      <header class="modal-header security-login-header">
        <div class="brand-cluster"><span class="brand-mark">PRISMIX</span></div>
      </header>
      <div class="modal-body security-login-body">
        <h1 class="security-login-title" id="securityGateTitle">SIGN IN TO THE CONSOLE</h1>
        <p class="security-login-copy" id="securityGateCopy">Enter your console PIN to continue.</p>
        <form id="loginForm" novalidate>
          <div class="system-field">
            <label for="loginPin">CONSOLE PIN</label>
            <div class="system-pin-wrap">
              <input class="system-pin-input" id="loginPin" name="pin" type="password"
                     inputmode="numeric" pattern="[0-9]*" maxlength="12" minlength="6"
                     autocomplete="current-password" enterkeyhint="done" required
                     aria-describedby="loginError">
              <button class="system-pin-reveal" type="button" data-pin-reveal="loginPin"
                      aria-label="Show PIN" aria-pressed="false">SHOW</button>
            </div>
          </div>
          <div class="system-dialog-error" id="loginError" role="alert" aria-live="assertive" hidden></div>
          <button class="hw-btn touch active security-login-submit" id="loginSubmit" type="submit">UNLOCK CONSOLE</button>
        </form>
      </div>
    </section>
  </div>

  <!-- Mixer lock gate: audio and DSP continue, controls are blocked. -->
  <div class="mixer-lock-gate" id="mixerLockGate" hidden>
    <section class="modal-dialog system-modal mixer-lock-card" role="dialog" aria-modal="true"
             aria-labelledby="mixerLockTitle" aria-describedby="mixerLockCopy" tabindex="-1">
      <header class="modal-header">
        <strong class="mixer-lock-title" id="mixerLockTitle">MIXER LOCKED</strong>
      </header>
      <div class="modal-body">
        <p class="mixer-lock-copy" id="mixerLockCopy">
          Audio controls are temporarily disabled. Audio processing and the current mix state remain unchanged. Unlock the console to resume control.
        </p>
        <button class="hw-btn touch active mixer-lock-unlock" type="button"
                onclick="window.prismix.openUnlockDialog()">UNLOCK MIXER</button>
      </div>
    </section>
  </div>

  <!-- Reusable brushed-metal dialog used by every System Console workflow. -->
  <div class="modal-backdrop" id="systemModalBackdrop" aria-hidden="true">
    <section class="modal-dialog system-modal" id="systemModal" role="dialog" aria-modal="true"
             aria-labelledby="systemModalTitle" aria-describedby="systemModalContext" tabindex="-1">
      <header class="modal-header">
        <strong id="systemModalTitle" style="font-size:13px;letter-spacing:0.75px;"></strong>
        <span class="system-dialog-context" id="systemModalContext"></span>
      </header>
      <div class="modal-body" id="systemModalBody"></div>
      <div class="system-dialog-error" id="systemModalError" role="alert" aria-live="assertive" hidden></div>
      <footer class="modal-footer" id="systemModalFooter"></footer>
    </section>
  </div>
  <div class="system-toast-stack" id="systemToastStack" aria-live="polite" aria-atomic="false"></div>

  <!-- Add FX / Preset / PatchStorage Browser Modal -->
  <div class="modal-backdrop" id="fxModalBackdrop" role="dialog" aria-modal="true" aria-labelledby="fxModalTitle">
    <div class="modal-dialog">
      <div class="modal-header">
        <div style="display:flex;align-items:center;justify-content:space-between;gap:8px;flex-wrap:wrap;">
          <strong id="fxModalTitle" style="font-size:13px;letter-spacing:0.8px;">INSERT FX &amp; LV2 PLUGIN CATALOG</strong>
          <div style="display:flex;gap:6px;align-items:center;flex:1;max-width:340px;">
            <input type="search" class="hw-input" id="fxModalSearch" placeholder="FILTER PLUGINS / PATCHSTORAGE..." oninput="window.prismix.onModalSearch(this.value)">
          </div>
        </div>
        <div class="modal-tabs" id="fxModalTabs">
          <button class="hw-btn modal-tab active" data-cat="ALL" type="button" onclick="window.prismix.setModalCategory('ALL')">ALL</button>
          <button class="hw-btn modal-tab" data-cat="BUILTIN" type="button" onclick="window.prismix.setModalCategory('BUILTIN')">BUILT-IN</button>
          <button class="hw-btn modal-tab" data-cat="LV2" type="button" onclick="window.prismix.setModalCategory('LV2')">LV2 PLUGINS</button>
          <button class="hw-btn modal-tab" data-cat="NAM" type="button" onclick="window.prismix.setModalCategory('NAM')">NAM MODELS</button>
          <button class="hw-btn modal-tab" data-cat="PRESETS" type="button" onclick="window.prismix.setModalCategory('PRESETS')">PRESETS</button>
          <button class="hw-btn modal-tab" data-cat="PATCHSTORAGE" type="button" onclick="window.prismix.setModalCategory('PATCHSTORAGE')">PATCHSTORAGE</button>
        </div>
      </div>
      <div class="modal-body" id="fxModalBody"></div>
      <div class="modal-footer">
        <div style="display:flex;align-items:center;gap:8px;" id="fxModalPagination"></div>
        <button class="hw-btn touch" type="button" onclick="window.prismix.closeFxModal()">CANCEL</button>
      </div>
    </div>
  </div>

  <!-- Shared channel operation dialog for Add, Rename, and Delete. -->
  <div class="modal-backdrop channel-dialog-backdrop" id="channelDialogBackdrop" aria-hidden="true">
    <section class="modal-dialog channel-dialog" id="channelDialog" role="dialog" aria-modal="true"
             aria-labelledby="channelDialogTitle" aria-describedby="channelDialogContext" tabindex="-1">
      <header class="modal-header channel-dialog-header">
        <strong class="channel-dialog-title" id="channelDialogTitle"></strong>
        <span class="channel-dialog-context" id="channelDialogContext"></span>
      </header>
      <form class="channel-dialog-form" id="channelDialogForm" novalidate>
        <div class="modal-body channel-dialog-body" id="channelDialogBody"></div>
        <div class="channel-dialog-error" id="channelDialogError" role="alert" aria-live="assertive" hidden></div>
        <footer class="modal-footer channel-dialog-footer" id="channelDialogFooter"></footer>
      </form>
    </section>
  </div>

  <!--
    QR Code Generator for JavaScript, v1.4.4
    Copyright (c) 2009 Kazuhiko Arase. MIT licensed.
    https://github.com/kazuhikoarase/qrcode-generator
    Vendored locally so the PRISMIX console remains fully offline.
  -->
  <script>
var qrcode=function(){var t=function(t,r){var e=t,n=g[r],o=null,i=0,a=null,u=[],f={},c=function(t,r){o=function(t){for(var r=new Array(t),e=0;e<t;e+=1){r[e]=new Array(t);for(var n=0;n<t;n+=1)r[e][n]=null}return r}(i=4*e+17),l(0,0),l(i-7,0),l(0,i-7),s(),h(),d(t,r),e>=7&&v(t),null==a&&(a=p(e,n,u)),w(a,r)},l=function(t,r){for(var e=-1;e<=7;e+=1)if(!(t+e<=-1||i<=t+e))for(var n=-1;n<=7;n+=1)r+n<=-1||i<=r+n||(o[t+e][r+n]=0<=e&&e<=6&&(0==n||6==n)||0<=n&&n<=6&&(0==e||6==e)||2<=e&&e<=4&&2<=n&&n<=4)},h=function(){for(var t=8;t<i-8;t+=1)null==o[t][6]&&(o[t][6]=t%2==0);for(var r=8;r<i-8;r+=1)null==o[6][r]&&(o[6][r]=r%2==0)},s=function(){for(var t=B.getPatternPosition(e),r=0;r<t.length;r+=1)for(var n=0;n<t.length;n+=1){var i=t[r],a=t[n];if(null==o[i][a])for(var u=-2;u<=2;u+=1)for(var f=-2;f<=2;f+=1)o[i+u][a+f]=-2==u||2==u||-2==f||2==f||0==u&&0==f}},v=function(t){for(var r=B.getBCHTypeNumber(e),n=0;n<18;n+=1){var a=!t&&1==(r>>n&1);o[Math.floor(n/3)][n%3+i-8-3]=a}for(n=0;n<18;n+=1){a=!t&&1==(r>>n&1);o[n%3+i-8-3][Math.floor(n/3)]=a}},d=function(t,r){for(var e=n<<3|r,a=B.getBCHTypeInfo(e),u=0;u<15;u+=1){var f=!t&&1==(a>>u&1);u<6?o[u][8]=f:u<8?o[u+1][8]=f:o[i-15+u][8]=f}for(u=0;u<15;u+=1){f=!t&&1==(a>>u&1);u<8?o[8][i-u-1]=f:u<9?o[8][15-u-1+1]=f:o[8][15-u-1]=f}o[i-8][8]=!t},w=function(t,r){for(var e=-1,n=i-1,a=7,u=0,f=B.getMaskFunction(r),c=i-1;c>0;c-=2)for(6==c&&(c-=1);;){for(var g=0;g<2;g+=1)if(null==o[n][c-g]){var l=!1;u<t.length&&(l=1==(t[u]>>>a&1)),f(n,c-g)&&(l=!l),o[n][c-g]=l,-1==(a-=1)&&(u+=1,a=7)}if((n+=e)<0||i<=n){n-=e,e=-e;break}}},p=function(t,r,e){for(var n=A.getRSBlocks(t,r),o=b(),i=0;i<e.length;i+=1){var a=e[i];o.put(a.getMode(),4),o.put(a.getLength(),B.getLengthInBits(a.getMode(),t)),a.write(o)}var u=0;for(i=0;i<n.length;i+=1)u+=n[i].dataCount;if(o.getLengthInBits()>8*u)throw"code length overflow. ("+o.getLengthInBits()+">"+8*u+")";for(o.getLengthInBits()+4<=8*u&&o.put(0,4);o.getLengthInBits()%8!=0;)o.putBit(!1);for(;!(o.getLengthInBits()>=8*u||(o.put(236,8),o.getLengthInBits()>=8*u));)o.put(17,8);return function(t,r){for(var e=0,n=0,o=0,i=new Array(r.length),a=new Array(r.length),u=0;u<r.length;u+=1){var f=r[u].dataCount,c=r[u].totalCount-f;n=Math.max(n,f),o=Math.max(o,c),i[u]=new Array(f);for(var g=0;g<i[u].length;g+=1)i[u][g]=255&t.getBuffer()[g+e];e+=f;var l=B.getErrorCorrectPolynomial(c),h=k(i[u],l.getLength()-1).mod(l);for(a[u]=new Array(l.getLength()-1),g=0;g<a[u].length;g+=1){var s=g+h.getLength()-a[u].length;a[u][g]=s>=0?h.getAt(s):0}}var v=0;for(g=0;g<r.length;g+=1)v+=r[g].totalCount;var d=new Array(v),w=0;for(g=0;g<n;g+=1)for(u=0;u<r.length;u+=1)g<i[u].length&&(d[w]=i[u][g],w+=1);for(g=0;g<o;g+=1)for(u=0;u<r.length;u+=1)g<a[u].length&&(d[w]=a[u][g],w+=1);return d}(o,n)};f.addData=function(t,r){var e=null;switch(r=r||"Byte"){case"Numeric":e=M(t);break;case"Alphanumeric":e=x(t);break;case"Byte":e=m(t);break;case"Kanji":e=L(t);break;default:throw"mode:"+r}u.push(e),a=null},f.isDark=function(t,r){if(t<0||i<=t||r<0||i<=r)throw t+","+r;return o[t][r]},f.getModuleCount=function(){return i},f.make=function(){if(e<1){for(var t=1;t<40;t++){for(var r=A.getRSBlocks(t,n),o=b(),i=0;i<u.length;i++){var a=u[i];o.put(a.getMode(),4),o.put(a.getLength(),B.getLengthInBits(a.getMode(),t)),a.write(o)}var g=0;for(i=0;i<r.length;i++)g+=r[i].dataCount;if(o.getLengthInBits()<=8*g)break}e=t}c(!1,function(){for(var t=0,r=0,e=0;e<8;e+=1){c(!0,e);var n=B.getLostPoint(f);(0==e||t>n)&&(t=n,r=e)}return r}())},f.createTableTag=function(t,r){t=t||2;var e="";e+='<table style="',e+=" border-width: 0px; border-style: none;",e+=" border-collapse: collapse;",e+=" padding: 0px; margin: "+(r=void 0===r?4*t:r)+"px;",e+='">',e+="<tbody>";for(var n=0;n<f.getModuleCount();n+=1){e+="<tr>";for(var o=0;o<f.getModuleCount();o+=1)e+='<td style="',e+=" border-width: 0px; border-style: none;",e+=" border-collapse: collapse;",e+=" padding: 0px; margin: 0px;",e+=" width: "+t+"px;",e+=" height: "+t+"px;",e+=" background-color: ",e+=f.isDark(n,o)?"#000000":"#ffffff",e+=";",e+='"/>';e+="</tr>"}return e+="</tbody>",e+="</table>"},f.createSvgTag=function(t,r,e,n){var o={};"object"==typeof arguments[0]&&(t=(o=arguments[0]).cellSize,r=o.margin,e=o.alt,n=o.title),t=t||2,r=void 0===r?4*t:r,(e="string"==typeof e?{text:e}:e||{}).text=e.text||null,e.id=e.text?e.id||"qrcode-description":null,(n="string"==typeof n?{text:n}:n||{}).text=n.text||null,n.id=n.text?n.id||"qrcode-title":null;var i,a,u,c,g=f.getModuleCount()*t+2*r,l="";for(c="l"+t+",0 0,"+t+" -"+t+",0 0,-"+t+"z ",l+='<svg version="1.1" xmlns="http://www.w3.org/2000/svg"',l+=o.scalable?"":' width="'+g+'px" height="'+g+'px"',l+=' viewBox="0 0 '+g+" "+g+'" ',l+=' preserveAspectRatio="xMinYMin meet"',l+=n.text||e.text?' role="img" aria-labelledby="'+y([n.id,e.id].join(" ").trim())+'"':"",l+=">",l+=n.text?'<title id="'+y(n.id)+'">'+y(n.text)+"</title>":"",l+=e.text?'<description id="'+y(e.id)+'">'+y(e.text)+"</description>":"",l+='<rect width="100%" height="100%" fill="white" cx="0" cy="0"/>',l+='<path d="',a=0;a<f.getModuleCount();a+=1)for(u=a*t+r,i=0;i<f.getModuleCount();i+=1)f.isDark(a,i)&&(l+="M"+(i*t+r)+","+u+c);return l+='" stroke="transparent" fill="black"/>',l+="</svg>"},f.createDataURL=function(t,r){t=t||2,r=void 0===r?4*t:r;var e=f.getModuleCount()*t+2*r,n=r,o=e-r;return I(e,e,(function(r,e){if(n<=r&&r<o&&n<=e&&e<o){var i=Math.floor((r-n)/t),a=Math.floor((e-n)/t);return f.isDark(a,i)?0:1}return 1}))},f.createImgTag=function(t,r,e){t=t||2,r=void 0===r?4*t:r;var n=f.getModuleCount()*t+2*r,o="";return o+="<img",o+=' src="',o+=f.createDataURL(t,r),o+='"',o+=' width="',o+=n,o+='"',o+=' height="',o+=n,o+='"',e&&(o+=' alt="',o+=y(e),o+='"'),o+="/>"};var y=function(t){for(var r="",e=0;e<t.length;e+=1){var n=t.charAt(e);switch(n){case"<":r+="&lt;";break;case">":r+="&gt;";break;case"&":r+="&amp;";break;case'"':r+="&quot;";break;default:r+=n}}return r};return f.createASCII=function(t,r){if((t=t||1)<2)return function(t){t=void 0===t?2:t;var r,e,n,o,i,a=1*f.getModuleCount()+2*t,u=t,c=a-t,g={"██":"█","█ ":"▀"," █":"▄","  ":" "},l={"██":"▀","█ ":"▀"," █":" ","  ":" "},h="";for(r=0;r<a;r+=2){for(n=Math.floor((r-u)/1),o=Math.floor((r+1-u)/1),e=0;e<a;e+=1)i="█",u<=e&&e<c&&u<=r&&r<c&&f.isDark(n,Math.floor((e-u)/1))&&(i=" "),u<=e&&e<c&&u<=r+1&&r+1<c&&f.isDark(o,Math.floor((e-u)/1))?i+=" ":i+="█",h+=t<1&&r+1>=c?l[i]:g[i];h+="\n"}return a%2&&t>0?h.substring(0,h.length-a-1)+Array(a+1).join("▀"):h.substring(0,h.length-1)}(r);t-=1,r=void 0===r?2*t:r;var e,n,o,i,a=f.getModuleCount()*t+2*r,u=r,c=a-r,g=Array(t+1).join("██"),l=Array(t+1).join("  "),h="",s="";for(e=0;e<a;e+=1){for(o=Math.floor((e-u)/t),s="",n=0;n<a;n+=1)i=1,u<=n&&n<c&&u<=e&&e<c&&f.isDark(o,Math.floor((n-u)/t))&&(i=0),s+=i?g:l;for(o=0;o<t;o+=1)h+=s+"\n"}return h.substring(0,h.length-1)},f.renderTo2dContext=function(t,r){r=r||2;for(var e=f.getModuleCount(),n=0;n<e;n++)for(var o=0;o<e;o++)t.fillStyle=f.isDark(n,o)?"black":"white",t.fillRect(n*r,o*r,r,r)},f};t.stringToBytes=(t.stringToBytesFuncs={default:function(t){for(var r=[],e=0;e<t.length;e+=1){var n=t.charCodeAt(e);r.push(255&n)}return r}}).default,t.createStringToBytes=function(t,r){var e=function(){for(var e=S(t),n=function(){var t=e.read();if(-1==t)throw"eof";return t},o=0,i={};;){var a=e.read();if(-1==a)break;var u=n(),f=n()<<8|n();i[String.fromCharCode(a<<8|u)]=f,o+=1}if(o!=r)throw o+" != "+r;return i}(),n="?".charCodeAt(0);return function(t){for(var r=[],o=0;o<t.length;o+=1){var i=t.charCodeAt(o);if(i<128)r.push(i);else{var a=e[t.charAt(o)];"number"==typeof a?(255&a)==a?r.push(a):(r.push(a>>>8),r.push(255&a)):r.push(n)}}return r}};var r,e,n,o,i,a=1,u=2,f=4,c=8,g={L:1,M:0,Q:3,H:2},l=0,h=1,s=2,v=3,d=4,w=5,p=6,y=7,B=(r=[[],[6,18],[6,22],[6,26],[6,30],[6,34],[6,22,38],[6,24,42],[6,26,46],[6,28,50],[6,30,54],[6,32,58],[6,34,62],[6,26,46,66],[6,26,48,70],[6,26,50,74],[6,30,54,78],[6,30,56,82],[6,30,58,86],[6,34,62,90],[6,28,50,72,94],[6,26,50,74,98],[6,30,54,78,102],[6,28,54,80,106],[6,32,58,84,110],[6,30,58,86,114],[6,34,62,90,118],[6,26,50,74,98,122],[6,30,54,78,102,126],[6,26,52,78,104,130],[6,30,56,82,108,134],[6,34,60,86,112,138],[6,30,58,86,114,142],[6,34,62,90,118,146],[6,30,54,78,102,126,150],[6,24,50,76,102,128,154],[6,28,54,80,106,132,158],[6,32,58,84,110,136,162],[6,26,54,82,110,138,166],[6,30,58,86,114,142,170]],e=1335,n=7973,i=function(t){for(var r=0;0!=t;)r+=1,t>>>=1;return r},(o={}).getBCHTypeInfo=function(t){for(var r=t<<10;i(r)-i(e)>=0;)r^=e<<i(r)-i(e);return 21522^(t<<10|r)},o.getBCHTypeNumber=function(t){for(var r=t<<12;i(r)-i(n)>=0;)r^=n<<i(r)-i(n);return t<<12|r},o.getPatternPosition=function(t){return r[t-1]},o.getMaskFunction=function(t){switch(t){case l:return function(t,r){return(t+r)%2==0};case h:return function(t,r){return t%2==0};case s:return function(t,r){return r%3==0};case v:return function(t,r){return(t+r)%3==0};case d:return function(t,r){return(Math.floor(t/2)+Math.floor(r/3))%2==0};case w:return function(t,r){return t*r%2+t*r%3==0};case p:return function(t,r){return(t*r%2+t*r%3)%2==0};case y:return function(t,r){return(t*r%3+(t+r)%2)%2==0};default:throw"bad maskPattern:"+t}},o.getErrorCorrectPolynomial=function(t){for(var r=k([1],0),e=0;e<t;e+=1)r=r.multiply(k([1,C.gexp(e)],0));return r},o.getLengthInBits=function(t,r){if(1<=r&&r<10)switch(t){case a:return 10;case u:return 9;case f:case c:return 8;default:throw"mode:"+t}else if(r<27)switch(t){case a:return 12;case u:return 11;case f:return 16;case c:return 10;default:throw"mode:"+t}else{if(!(r<41))throw"type:"+r;switch(t){case a:return 14;case u:return 13;case f:return 16;case c:return 12;default:throw"mode:"+t}}},o.getLostPoint=function(t){for(var r=t.getModuleCount(),e=0,n=0;n<r;n+=1)for(var o=0;o<r;o+=1){for(var i=0,a=t.isDark(n,o),u=-1;u<=1;u+=1)if(!(n+u<0||r<=n+u))for(var f=-1;f<=1;f+=1)o+f<0||r<=o+f||0==u&&0==f||a==t.isDark(n+u,o+f)&&(i+=1);i>5&&(e+=3+i-5)}for(n=0;n<r-1;n+=1)for(o=0;o<r-1;o+=1){var c=0;t.isDark(n,o)&&(c+=1),t.isDark(n+1,o)&&(c+=1),t.isDark(n,o+1)&&(c+=1),t.isDark(n+1,o+1)&&(c+=1),0!=c&&4!=c||(e+=3)}for(n=0;n<r;n+=1)for(o=0;o<r-6;o+=1)t.isDark(n,o)&&!t.isDark(n,o+1)&&t.isDark(n,o+2)&&t.isDark(n,o+3)&&t.isDark(n,o+4)&&!t.isDark(n,o+5)&&t.isDark(n,o+6)&&(e+=40);for(o=0;o<r;o+=1)for(n=0;n<r-6;n+=1)t.isDark(n,o)&&!t.isDark(n+1,o)&&t.isDark(n+2,o)&&t.isDark(n+3,o)&&t.isDark(n+4,o)&&!t.isDark(n+5,o)&&t.isDark(n+6,o)&&(e+=40);var g=0;for(o=0;o<r;o+=1)for(n=0;n<r;n+=1)t.isDark(n,o)&&(g+=1);return e+=Math.abs(100*g/r/r-50)/5*10},o),C=function(){for(var t=new Array(256),r=new Array(256),e=0;e<8;e+=1)t[e]=1<<e;for(e=8;e<256;e+=1)t[e]=t[e-4]^t[e-5]^t[e-6]^t[e-8];for(e=0;e<255;e+=1)r[t[e]]=e;var n={glog:function(t){if(t<1)throw"glog("+t+")";return r[t]},gexp:function(r){for(;r<0;)r+=255;for(;r>=256;)r-=255;return t[r]}};return n}();function k(t,r){if(void 0===t.length)throw t.length+"/"+r;var e=function(){for(var e=0;e<t.length&&0==t[e];)e+=1;for(var n=new Array(t.length-e+r),o=0;o<t.length-e;o+=1)n[o]=t[o+e];return n}(),n={getAt:function(t){return e[t]},getLength:function(){return e.length},multiply:function(t){for(var r=new Array(n.getLength()+t.getLength()-1),e=0;e<n.getLength();e+=1)for(var o=0;o<t.getLength();o+=1)r[e+o]^=C.gexp(C.glog(n.getAt(e))+C.glog(t.getAt(o)));return k(r,0)},mod:function(t){if(n.getLength()-t.getLength()<0)return n;for(var r=C.glog(n.getAt(0))-C.glog(t.getAt(0)),e=new Array(n.getLength()),o=0;o<n.getLength();o+=1)e[o]=n.getAt(o);for(o=0;o<t.getLength();o+=1)e[o]^=C.gexp(C.glog(t.getAt(o))+r);return k(e,0).mod(t)}};return n}var A=function(){var t=[[1,26,19],[1,26,16],[1,26,13],[1,26,9],[1,44,34],[1,44,28],[1,44,22],[1,44,16],[1,70,55],[1,70,44],[2,35,17],[2,35,13],[1,100,80],[2,50,32],[2,50,24],[4,25,9],[1,134,108],[2,67,43],[2,33,15,2,34,16],[2,33,11,2,34,12],[2,86,68],[4,43,27],[4,43,19],[4,43,15],[2,98,78],[4,49,31],[2,32,14,4,33,15],[4,39,13,1,40,14],[2,121,97],[2,60,38,2,61,39],[4,40,18,2,41,19],[4,40,14,2,41,15],[2,146,116],[3,58,36,2,59,37],[4,36,16,4,37,17],[4,36,12,4,37,13],[2,86,68,2,87,69],[4,69,43,1,70,44],[6,43,19,2,44,20],[6,43,15,2,44,16],[4,101,81],[1,80,50,4,81,51],[4,50,22,4,51,23],[3,36,12,8,37,13],[2,116,92,2,117,93],[6,58,36,2,59,37],[4,46,20,6,47,21],[7,42,14,4,43,15],[4,133,107],[8,59,37,1,60,38],[8,44,20,4,45,21],[12,33,11,4,34,12],[3,145,115,1,146,116],[4,64,40,5,65,41],[11,36,16,5,37,17],[11,36,12,5,37,13],[5,109,87,1,110,88],[5,65,41,5,66,42],[5,54,24,7,55,25],[11,36,12,7,37,13],[5,122,98,1,123,99],[7,73,45,3,74,46],[15,43,19,2,44,20],[3,45,15,13,46,16],[1,135,107,5,136,108],[10,74,46,1,75,47],[1,50,22,15,51,23],[2,42,14,17,43,15],[5,150,120,1,151,121],[9,69,43,4,70,44],[17,50,22,1,51,23],[2,42,14,19,43,15],[3,141,113,4,142,114],[3,70,44,11,71,45],[17,47,21,4,48,22],[9,39,13,16,40,14],[3,135,107,5,136,108],[3,67,41,13,68,42],[15,54,24,5,55,25],[15,43,15,10,44,16],[4,144,116,4,145,117],[17,68,42],[17,50,22,6,51,23],[19,46,16,6,47,17],[2,139,111,7,140,112],[17,74,46],[7,54,24,16,55,25],[34,37,13],[4,151,121,5,152,122],[4,75,47,14,76,48],[11,54,24,14,55,25],[16,45,15,14,46,16],[6,147,117,4,148,118],[6,73,45,14,74,46],[11,54,24,16,55,25],[30,46,16,2,47,17],[8,132,106,4,133,107],[8,75,47,13,76,48],[7,54,24,22,55,25],[22,45,15,13,46,16],[10,142,114,2,143,115],[19,74,46,4,75,47],[28,50,22,6,51,23],[33,46,16,4,47,17],[8,152,122,4,153,123],[22,73,45,3,74,46],[8,53,23,26,54,24],[12,45,15,28,46,16],[3,147,117,10,148,118],[3,73,45,23,74,46],[4,54,24,31,55,25],[11,45,15,31,46,16],[7,146,116,7,147,117],[21,73,45,7,74,46],[1,53,23,37,54,24],[19,45,15,26,46,16],[5,145,115,10,146,116],[19,75,47,10,76,48],[15,54,24,25,55,25],[23,45,15,25,46,16],[13,145,115,3,146,116],[2,74,46,29,75,47],[42,54,24,1,55,25],[23,45,15,28,46,16],[17,145,115],[10,74,46,23,75,47],[10,54,24,35,55,25],[19,45,15,35,46,16],[17,145,115,1,146,116],[14,74,46,21,75,47],[29,54,24,19,55,25],[11,45,15,46,46,16],[13,145,115,6,146,116],[14,74,46,23,75,47],[44,54,24,7,55,25],[59,46,16,1,47,17],[12,151,121,7,152,122],[12,75,47,26,76,48],[39,54,24,14,55,25],[22,45,15,41,46,16],[6,151,121,14,152,122],[6,75,47,34,76,48],[46,54,24,10,55,25],[2,45,15,64,46,16],[17,152,122,4,153,123],[29,74,46,14,75,47],[49,54,24,10,55,25],[24,45,15,46,46,16],[4,152,122,18,153,123],[13,74,46,32,75,47],[48,54,24,14,55,25],[42,45,15,32,46,16],[20,147,117,4,148,118],[40,75,47,7,76,48],[43,54,24,22,55,25],[10,45,15,67,46,16],[19,148,118,6,149,119],[18,75,47,31,76,48],[34,54,24,34,55,25],[20,45,15,61,46,16]],r=function(t,r){var e={};return e.totalCount=t,e.dataCount=r,e},e={};return e.getRSBlocks=function(e,n){var o=function(r,e){switch(e){case g.L:return t[4*(r-1)+0];case g.M:return t[4*(r-1)+1];case g.Q:return t[4*(r-1)+2];case g.H:return t[4*(r-1)+3];default:return}}(e,n);if(void 0===o)throw"bad rs block @ typeNumber:"+e+"/errorCorrectionLevel:"+n;for(var i=o.length/3,a=[],u=0;u<i;u+=1)for(var f=o[3*u+0],c=o[3*u+1],l=o[3*u+2],h=0;h<f;h+=1)a.push(r(c,l));return a},e}(),b=function(){var t=[],r=0,e={getBuffer:function(){return t},getAt:function(r){var e=Math.floor(r/8);return 1==(t[e]>>>7-r%8&1)},put:function(t,r){for(var n=0;n<r;n+=1)e.putBit(1==(t>>>r-n-1&1))},getLengthInBits:function(){return r},putBit:function(e){var n=Math.floor(r/8);t.length<=n&&t.push(0),e&&(t[n]|=128>>>r%8),r+=1}};return e},M=function(t){var r=a,e=t,n={getMode:function(){return r},getLength:function(t){return e.length},write:function(t){for(var r=e,n=0;n+2<r.length;)t.put(o(r.substring(n,n+3)),10),n+=3;n<r.length&&(r.length-n==1?t.put(o(r.substring(n,n+1)),4):r.length-n==2&&t.put(o(r.substring(n,n+2)),7))}},o=function(t){for(var r=0,e=0;e<t.length;e+=1)r=10*r+i(t.charAt(e));return r},i=function(t){if("0"<=t&&t<="9")return t.charCodeAt(0)-"0".charCodeAt(0);throw"illegal char :"+t};return n},x=function(t){var r=u,e=t,n={getMode:function(){return r},getLength:function(t){return e.length},write:function(t){for(var r=e,n=0;n+1<r.length;)t.put(45*o(r.charAt(n))+o(r.charAt(n+1)),11),n+=2;n<r.length&&t.put(o(r.charAt(n)),6)}},o=function(t){if("0"<=t&&t<="9")return t.charCodeAt(0)-"0".charCodeAt(0);if("A"<=t&&t<="Z")return t.charCodeAt(0)-"A".charCodeAt(0)+10;switch(t){case" ":return 36;case"$":return 37;case"%":return 38;case"*":return 39;case"+":return 40;case"-":return 41;case".":return 42;case"/":return 43;case":":return 44;default:throw"illegal char :"+t}};return n},m=function(r){var e=f,n=t.stringToBytes(r),o={getMode:function(){return e},getLength:function(t){return n.length},write:function(t){for(var r=0;r<n.length;r+=1)t.put(n[r],8)}};return o},L=function(r){var e=c,n=t.stringToBytesFuncs.SJIS;if(!n)throw"sjis not supported.";!function(){var t=n("友");if(2!=t.length||38726!=(t[0]<<8|t[1]))throw"sjis not supported."}();var o=n(r),i={getMode:function(){return e},getLength:function(t){return~~(o.length/2)},write:function(t){for(var r=o,e=0;e+1<r.length;){var n=(255&r[e])<<8|255&r[e+1];if(33088<=n&&n<=40956)n-=33088;else{if(!(57408<=n&&n<=60351))throw"illegal char at "+(e+1)+"/"+n;n-=49472}n=192*(n>>>8&255)+(255&n),t.put(n,13),e+=2}if(e<r.length)throw"illegal char at "+(e+1)}};return i},D=function(){var t=[],r={writeByte:function(r){t.push(255&r)},writeShort:function(t){r.writeByte(t),r.writeByte(t>>>8)},writeBytes:function(t,e,n){e=e||0,n=n||t.length;for(var o=0;o<n;o+=1)r.writeByte(t[o+e])},writeString:function(t){for(var e=0;e<t.length;e+=1)r.writeByte(t.charCodeAt(e))},toByteArray:function(){return t},toString:function(){var r="";r+="[";for(var e=0;e<t.length;e+=1)e>0&&(r+=","),r+=t[e];return r+="]"}};return r},S=function(t){var r=t,e=0,n=0,o=0,i={read:function(){for(;o<8;){if(e>=r.length){if(0==o)return-1;throw"unexpected end of file./"+o}var t=r.charAt(e);if(e+=1,"="==t)return o=0,-1;t.match(/^\s$/)||(n=n<<6|a(t.charCodeAt(0)),o+=6)}var i=n>>>o-8&255;return o-=8,i}},a=function(t){if(65<=t&&t<=90)return t-65;if(97<=t&&t<=122)return t-97+26;if(48<=t&&t<=57)return t-48+52;if(43==t)return 62;if(47==t)return 63;throw"c:"+t};return i},I=function(t,r,e){for(var n=function(t,r){var e=t,n=r,o=new Array(t*r),i={setPixel:function(t,r,n){o[r*e+t]=n},write:function(t){t.writeString("GIF87a"),t.writeShort(e),t.writeShort(n),t.writeByte(128),t.writeByte(0),t.writeByte(0),t.writeByte(0),t.writeByte(0),t.writeByte(0),t.writeByte(255),t.writeByte(255),t.writeByte(255),t.writeString(","),t.writeShort(0),t.writeShort(0),t.writeShort(e),t.writeShort(n),t.writeByte(0);var r=a(2);t.writeByte(2);for(var o=0;r.length-o>255;)t.writeByte(255),t.writeBytes(r,o,255),o+=255;t.writeByte(r.length-o),t.writeBytes(r,o,r.length-o),t.writeByte(0),t.writeString(";")}},a=function(t){for(var r=1<<t,e=1+(1<<t),n=t+1,i=u(),a=0;a<r;a+=1)i.add(String.fromCharCode(a));i.add(String.fromCharCode(r)),i.add(String.fromCharCode(e));var f,c,g,l=D(),h=(f=l,c=0,g=0,{write:function(t,r){if(t>>>r!=0)throw"length over";for(;c+r>=8;)f.writeByte(255&(t<<c|g)),r-=8-c,t>>>=8-c,g=0,c=0;g|=t<<c,c+=r},flush:function(){c>0&&f.writeByte(g)}});h.write(r,n);var s=0,v=String.fromCharCode(o[s]);for(s+=1;s<o.length;){var d=String.fromCharCode(o[s]);s+=1,i.contains(v+d)?v+=d:(h.write(i.indexOf(v),n),i.size()<4095&&(i.size()==1<<n&&(n+=1),i.add(v+d)),v=d)}return h.write(i.indexOf(v),n),h.write(e,n),h.flush(),l.toByteArray()},u=function(){var t={},r=0,e={add:function(n){if(e.contains(n))throw"dup key:"+n;t[n]=r,r+=1},size:function(){return r},indexOf:function(r){return t[r]},contains:function(r){return void 0!==t[r]}};return e};return i}(t,r),o=0;o<r;o+=1)for(var i=0;i<t;i+=1)n.setPixel(i,o,e(i,o));var a=D();n.write(a);for(var u=function(){var t=0,r=0,e=0,n="",o={},i=function(t){n+=String.fromCharCode(a(63&t))},a=function(t){if(t<0);else{if(t<26)return 65+t;if(t<52)return t-26+97;if(t<62)return t-52+48;if(62==t)return 43;if(63==t)return 47}throw"n:"+t};return o.writeByte=function(n){for(t=t<<8|255&n,r+=8,e+=1;r>=6;)i(t>>>r-6),r-=6},o.flush=function(){if(r>0&&(i(t<<6-r),t=0,r=0),e%3!=0)for(var o=3-e%3,a=0;a<o;a+=1)n+="="},o.toString=function(){return n},o}(),f=a.toByteArray(),c=0;c<f.length;c+=1)u.writeByte(f[c]);return u.flush(),"data:image/gif;base64,"+u};return t}();qrcode.stringToBytesFuncs["UTF-8"]=function(t){return function(t){for(var r=[],e=0;e<t.length;e++){var n=t.charCodeAt(e);n<128?r.push(n):n<2048?r.push(192|n>>6,128|63&n):n<55296||n>=57344?r.push(224|n>>12,128|n>>6&63,128|63&n):(e++,n=65536+((1023&n)<<10|1023&t.charCodeAt(e)),r.push(240|n>>18,128|n>>12&63,128|n>>6&63,128|63&n))}return r}(t)},function(t){"function"==typeof define&&define.amd?define([],t):"object"==typeof exports&&(module.exports=t())}((function(){return qrcode}));
//# sourceMappingURL=/sm/26b4b0d0b1e283d6b3ec9857ac597d7a60c76ac17be1ef4c965f03086de426bb.map
  </script>

  <script>
  (() => {
    "use strict";

    // Authoritative Console State & Telemetry Store
    const store = {
      state: null,
      telemetry: null,
      catalog: { plugins: [], presets: [], geq_frequencies: [], geq_labels: [], patchstorage: { items: [], current_page: 1, total_pages: 1 } },
      activeView: 0, // 0:Mixer, 1:Channel, 2:EQ, 3:Dynamics, 4:FX, 5:Routing, 6:System
      activeEqBand: 0,
      activeFxSlot: 0,
      routingSubMode: 0,
      systemSubMode: 0,
      modalOpen: false,
      modalCategory: "ALL",
      modalSearch: "",
      modalPage: 1,
      connected: false,
      connectionAttempted: false,
      activePointerKey: null,
      nativeRangePointerId: null,
      nativeRangeElement: null,
      rangeSettleUntil: 0,
      rangeKeyboardUntil: 0,
      rangeSettleToken: 0,
      draggingEqBand: -1,
      security: {
        loaded: false,
        configured: false,
        locked: false,
        authenticated: false,
        lan_access: false,
        pin_required: false,
        session_remaining: 0,
        bind_address: "",
        port: 0
      },
      systemStatus: null,
      systemStatusError: "",
      systemStatusBusy: false,
      systemModalOpen: false,
      systemModalActions: {},
      systemModalReturnFocus: null,
      systemModalBusy: false,
      wifiNetworks: null,
      authGateActive: false,
      lockGateCloseTimer: null
    };

    // Bounded Command Queue for Zero-Jitter Touch/Pointer Controls
    const coalescedTimers = new Map();
    const pendingPayloads = new Map();

    // ------------------------------------------------------------------
    // Shared security/session plumbing
    // ------------------------------------------------------------------
    function pinLooksValid(value) {
      return /^[0-9]{6,12}$/.test(String(value || ""));
    }

    function formatRemaining(seconds) {
      const total = Math.max(0, Number(seconds) || 0);
      if (total <= 0) return "EXPIRED";
      const h = Math.floor(total / 3600);
      const m = Math.floor((total % 3600) / 60);
      if (h > 0) return `${h}h ${String(m).padStart(2, "0")}m`;
      return `${Math.max(1, m)}m`;
    }

    function showToast(message, kind = "", duration = 3600) {
      const stack = document.getElementById("systemToastStack");
      if (!stack) return;
      const item = document.createElement("div");
      item.className = "system-toast " + (kind || "");
      item.textContent = message;
      stack.appendChild(item);
      while (stack.children.length > 3) stack.removeChild(stack.firstElementChild);
      setTimeout(() => {
        if (item.parentNode) item.parentNode.removeChild(item);
      }, Math.max(1200, duration));
    }

    function setConsoleInteractive(interactive) {
      const shell = document.getElementById("consoleShell");
      if (!shell) return;
      shell.inert = !interactive;
      if (interactive) shell.removeAttribute("inert");
      else shell.setAttribute("inert", "");
    }

    function clearSecurityGates() {
      const gate = document.getElementById("securityGate");
      if (gate) gate.hidden = true;
      hideLockGate();
      store.authGateActive = false;
      setConsoleInteractive(true);
    }

    function showAuthGate(errorText = "") {
      if (store.security.locked) {
        showLockGate();
        return;
      }
      if (!store.security.configured) {
        clearSecurityGates();
        return;
      }
      closeSystemModal();
      stopSse();
      store.authGateActive = true;
      const gate = document.getElementById("securityGate");
      hideLockGate(true);
      if (gate) gate.hidden = false;
      setConsoleInteractive(false);
      const error = document.getElementById("loginError");
      if (error) {
        error.textContent = errorText || "";
        error.hidden = !errorText;
      }
      const input = document.getElementById("loginPin");
      if (input) {
        if (errorText) input.value = "";
        input.setAttribute("aria-invalid", "false");
        if (input.type !== "password") {
          input.type = "password";
          const reveal = gate ? gate.querySelector('[data-pin-reveal="loginPin"]') : null;
          if (reveal) {
            reveal.textContent = "SHOW";
            reveal.setAttribute("aria-label", "Show PIN");
            reveal.setAttribute("aria-pressed", "false");
          }
        }
        setTimeout(() => input.focus(), 30);
      }
    }

    function showLockGate() {
      closeSystemModal();
      stopSse();
      store.security.locked = true;
      store.authGateActive = false;
      const gate = document.getElementById("securityGate");
      const lockGate = document.getElementById("mixerLockGate");
      if (gate) gate.hidden = true;
      if (store.lockGateCloseTimer) {
        clearTimeout(store.lockGateCloseTimer);
        store.lockGateCloseTimer = null;
      }
      if (lockGate) {
        lockGate.classList.remove("closing");
        lockGate.inert = false;
        lockGate.removeAttribute("aria-hidden");
        lockGate.hidden = false;
      }
      setConsoleInteractive(false);
      renderShellStatus();
      renderChannelSelectorStrip();
      renderActiveView();
      const card = lockGate ? lockGate.querySelector(".mixer-lock-card") : null;
      if (card) {
        setTimeout(() => {
          if (lockGate && !lockGate.hidden && !store.systemModalOpen) card.focus();
        }, 30);
      }
    }

    function hideLockGate(immediate = false) {
      const gate = document.getElementById("mixerLockGate");
      if (!gate || gate.hidden) return;
      if (gate.classList.contains("closing") && !immediate) return;
      const reducedMotion = typeof window.matchMedia === "function" &&
        window.matchMedia("(prefers-reduced-motion: reduce)").matches;
      if (store.lockGateCloseTimer) {
        clearTimeout(store.lockGateCloseTimer);
        store.lockGateCloseTimer = null;
      }
      const finish = () => {
        gate.hidden = true;
        gate.classList.remove("closing");
        gate.inert = false;
        gate.removeAttribute("aria-hidden");
        store.lockGateCloseTimer = null;
      };
      if (immediate || reducedMotion) {
        finish();
        return;
      }
      gate.classList.add("closing");
      gate.inert = true;
      gate.setAttribute("aria-hidden", "true");
      store.lockGateCloseTimer = setTimeout(finish, 180);
    }

    function handleSessionLost(errorText = "") {
      if (!store.security.configured) return;
      store.security.authenticated = false;
      store.state = null;
      store.telemetry = null;
      stopSse();
      showAuthGate(errorText || "");
    }

    function handleLocked() {
      store.security.authenticated = false;
      showLockGate();
    }

    function apiRequest(path, options = {}, authHandling = true) {
      const opts = Object.assign({ credentials: "same-origin", cache: "no-store" }, options);
      return fetch(path, opts)
        .then(async res => {
          const text = await res.text();
          let data = null;
          if (text) {
            try { data = JSON.parse(text); } catch (_) { data = { error: text }; }
          }
          if (authHandling) {
            if (res.status === 401) {
              handleSessionLost(data && data.error ? data.error : "");
            } else if (res.status === 423 && data && data.locked) {
              handleLocked();
            } else if (res.status === 403 && data && data.auth_required) {
              handleSessionLost(data.error || "");
            }
          }
          return { res, ok: res.ok, status: res.status, data };
        })
        .catch(err => {
          setConnected(false);
          return { res: null, ok: false, status: 0, data: { success: false, error: String(err) } };
        });
    }

    function apiGet(path) {
      return apiRequest(path, { method: "GET" });
    }

    function apiPost(path, body, authHandling = true) {
      return apiRequest(path, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body || {})
      }, authHandling);
    }

    async function fetchSecurityStatus(opts = {}) {
      const wasGate = store.authGateActive;
      const { ok, data } = await apiRequest("/api/auth/status", { method: "GET" }, false);
      if (!ok || !data) return null;
      store.security.loaded = true;
      store.security.configured = Boolean(data.configured);
      store.security.locked = Boolean(data.locked);
      store.security.authenticated = Boolean(data.authenticated);
      store.security.lan_access = Boolean(data.lan_access);
      store.security.pin_required = Boolean(data.pin_required);
      store.security.session_remaining = Number(data.session_remaining || 0);
      store.security.bind_address = String(data.bind_address || "");
      store.security.port = Number(data.port || 0);

      if (store.security.configured && store.security.locked) {
        const lockGate = document.getElementById("mixerLockGate");
        if (!lockGate || lockGate.hidden) showLockGate();
        return data;
      }
      if (store.security.configured && !store.security.authenticated) {
        if (!store.authGateActive) showAuthGate();
        return data;
      }

      clearSecurityGates();
      if ((wasGate || !store.state) && !store.systemModalOpen) {
        await enterConsole();
      } else if (store.activeView === 6 && store.systemSubMode === 2) {
        renderActiveView();
      }
      return data;
    }

    async function enterConsole(forceState = false) {
      clearSecurityGates();
      if (!store.state || forceState) await fetchState();
      await fetchCatalog();
      connectSse();
      refreshSystemStatus();
      if (store.activeView === 6) renderActiveView();
    }

    async function refreshSystemStatus(force = false) {
      if (!store.security.authenticated && store.security.configured) return null;
      if (store.systemStatusBusy && !force) return store.systemStatus;
      store.systemStatusBusy = true;
      try {
        const { ok, data } = await apiGet("/api/system/status" + (force ? "?refresh=1" : ""));
        if (ok && data) {
          store.systemStatus = data;
          store.systemStatusError = "";
          if (store.activeView === 6 && store.systemSubMode === 2) renderActiveView();
          return data;
        }
        store.systemStatusError = (data && data.error) || "System status unavailable.";
        return null;
      } finally {
        store.systemStatusBusy = false;
      }
    }

    async function sendCommand(payload, refreshState = true) {
      if (store.security.configured &&
          (!store.security.authenticated || store.security.locked)) {
        return { success: false, error: "Console authentication required." };
      }
      const { ok, data } = await apiPost("/api/command", payload);
      if (refreshState && !store.activePointerKey && !store.security.locked) {
        await fetchState();
      }
      if (!ok && data) {
        return Object.assign({ success: false }, data);
      }
      return data || { success: false, error: "No response from the control service." };
    }

    // ------------------------------------------------------------------
    // Reusable system dialog
    // ------------------------------------------------------------------
    function dialogFocusable(container) {
      if (!container) return [];
      return Array.from(container.querySelectorAll(
        'button:not(:disabled), [href], input:not(:disabled), select:not(:disabled), textarea:not(:disabled), [tabindex]:not([tabindex="-1"])'
      )).filter(el => el.getClientRects().length > 0);
    }

    function handleLockGateKeydown(event) {
      if (event.key !== "Tab" || store.systemModalOpen) return;
      const gate = document.getElementById("mixerLockGate");
      if (!gate || gate.hidden || gate.classList.contains("closing")) return;
      const card = gate.querySelector(".mixer-lock-card");
      const focusables = dialogFocusable(gate);
      if (!focusables.length) {
        event.preventDefault();
        if (card) card.focus();
        return;
      }
      const first = focusables[0];
      const last = focusables[focusables.length - 1];
      if (event.shiftKey && (document.activeElement === first || document.activeElement === card)) {
        event.preventDefault();
        last.focus();
      } else if (!event.shiftKey && document.activeElement === last) {
        event.preventDefault();
        first.focus();
      }
    }

    function handleSystemModalKeydown(event) {
      if (!store.systemModalOpen) return;
      if (event.key === "Escape") {
        if (!store.systemModalBusy) {
          event.preventDefault();
          closeSystemModal();
        }
        return;
      }
      if (event.key !== "Tab") return;
      const modal = document.getElementById("systemModal");
      const focusables = dialogFocusable(modal);
      if (!focusables.length) {
        event.preventDefault();
        if (modal) modal.focus();
        return;
      }
      const first = focusables[0];
      const last = focusables[focusables.length - 1];
      if (event.shiftKey && document.activeElement === first) {
        event.preventDefault();
        last.focus();
      } else if (!event.shiftKey && document.activeElement === last) {
        event.preventDefault();
        first.focus();
      }
    }

    function openSystemModal(config) {
      const backdrop = document.getElementById("systemModalBackdrop");
      const modal = document.getElementById("systemModal");
      const body = document.getElementById("systemModalBody");
      const footer = document.getElementById("systemModalFooter");
      const error = document.getElementById("systemModalError");
      if (!backdrop || !modal || !body || !footer) return;
      if (store.systemModalOpen) closeSystemModal();

      store.systemModalReturnFocus = document.activeElement;
      store.systemModalOpen = true;
      const shell = document.getElementById("consoleShell");
      if (shell) shell.inert = true;
      const lockGate = document.getElementById("mixerLockGate");
      if (lockGate && !lockGate.hidden) lockGate.inert = true;
      store.systemModalBusy = false;
      store.systemModalActions = config.actions || {};
      document.getElementById("systemModalTitle").textContent = config.title || "SYSTEM";
      document.getElementById("systemModalContext").textContent = config.context || "";
      body.innerHTML = config.body || "";
      error.textContent = "";
      error.hidden = true;
      const actions = config.actionButtons || [];
      footer.innerHTML = actions.map(action =>
        `<button class="hw-btn touch ${action.kind || ""}" type="button" data-sm-action="${esc(action.action)}"${action.disabled ? " disabled" : ""}>${esc(action.label)}</button>`
      ).join("");
      footer.style.display = actions.length ? "" : "none";
      backdrop.classList.add("open");
      backdrop.setAttribute("aria-hidden", "false");
      modal.focus();
      if (typeof config.afterOpen === "function") {
        requestAnimationFrame(() => config.afterOpen());
      }
      const autofocus = body.querySelector("[autofocus]");
      if (autofocus) setTimeout(() => autofocus.focus(), 40);
    }

    function closeSystemModal(restoreFocus = true) {
      const backdrop = document.getElementById("systemModalBackdrop");
      if (!backdrop) return;
      backdrop.classList.remove("open");
      backdrop.setAttribute("aria-hidden", "true");
      store.systemModalOpen = false;
      store.systemModalBusy = false;
      store.systemModalActions = {};
      const shell = document.getElementById("consoleShell");
      const lockGate = document.getElementById("mixerLockGate");
      const authGate = document.getElementById("securityGate");
      if (shell) {
        shell.inert = Boolean(
          (lockGate && !lockGate.hidden) || (authGate && !authGate.hidden)
        );
      }
      if (lockGate && !lockGate.hidden && !lockGate.classList.contains("closing")) {
        lockGate.inert = false;
      }
      const target = store.systemModalReturnFocus;
      store.systemModalReturnFocus = null;
      if (restoreFocus && target && target.focus && document.contains(target)) {
        setTimeout(() => target.focus(), 20);
      }
    }

    async function runSystemModalAction(actionName) {
      if (!store.systemModalOpen || store.systemModalBusy) return;
      const handler = store.systemModalActions[actionName];
      if (typeof handler !== "function") return;
      await handler();
    }

    function setSystemModalBusy(busy, label = "") {
      store.systemModalBusy = Boolean(busy);
      const footer = document.getElementById("systemModalFooter");
      if (!footer) return;
      footer.querySelectorAll("button").forEach(button => {
        if (!button.dataset.originalLabel) button.dataset.originalLabel = button.textContent;
        button.disabled = Boolean(busy);
        if (busy && button.dataset.smAction === "primary" && label) {
          button.textContent = label;
        } else if (!busy && button.dataset.originalLabel) {
          button.textContent = button.dataset.originalLabel;
        }
      });
    }

    function setSystemModalError(message) {
      const error = document.getElementById("systemModalError");
      if (!error) return;
      error.textContent = message || "";
      error.hidden = !message;
    }

    function setSystemModalBody(html, afterRender) {
      const body = document.getElementById("systemModalBody");
      if (!body) return;
      body.innerHTML = html;
      if (typeof afterRender === "function") requestAnimationFrame(afterRender);
    }

    function sendCoalescedCommand(key, payload, intervalMs = 28) {
      pendingPayloads.set(key, payload);
      if (coalescedTimers.has(key)) return;
      const timerId = setTimeout(async () => {
        coalescedTimers.delete(key);
        const latest = pendingPayloads.get(key);
        if (latest) {
          pendingPayloads.delete(key);
          await sendCommand(latest, false);
        }
      }, intervalMs);
      coalescedTimers.set(key, timerId);
    }

    async function flushCoalescedCommand(key) {
      if (coalescedTimers.has(key)) {
        clearTimeout(coalescedTimers.get(key));
        coalescedTimers.delete(key);
      }
      if (pendingPayloads.has(key)) {
        const finalPayload = pendingPayloads.get(key);
        pendingPayloads.delete(key);
        await sendCommand(finalPayload, true);
      } else {
        await fetchState();
      }
    }

    // Native ranges retain their built-in pointer, track, thumb and keyboard
    // behavior. During a drag we only defer state-driven view re-renders, so the
    // real input is never torn out from under the finger. `pan-y` keeps vertical
    // page scrolling available while horizontal movement stays with the slider.
    const NATIVE_RANGE_SELECTOR = 'input[type="range"].hw-range, input[type="range"].work-range, input[type="range"].geq-v-slider';

    function updateRangeFill(range) {
      if (!range || range.type !== "range") return;
      const min = Number(range.min);
      const max = Number(range.max);
      const value = Number(range.value);
      const pct = max > min ? clamp((value - min) / (max - min), 0, 1) * 100 : 0;
      range.style.setProperty("--range-pct", pct.toFixed(3) + "%");
    }

    function syncRangeFills(root = document) {
      const ranges = [];
      if (root.matches && root.matches(NATIVE_RANGE_SELECTOR)) ranges.push(root);
      if (root.querySelectorAll) root.querySelectorAll(NATIVE_RANGE_SELECTOR).forEach(r => ranges.push(r));
      ranges.forEach(updateRangeFill);
    }

    function shouldDeferLiveRender() {
      return Boolean(store.activePointerKey) ||
        store.nativeRangePointerId !== null ||
        performance.now() < store.rangeSettleUntil ||
        performance.now() < store.rangeKeyboardUntil ||
        pendingPayloads.size > 0 ||
        coalescedTimers.size > 0;
    }

    function flushPendingRangeCommands() {
      Array.from(pendingPayloads.keys()).forEach(key => {
        flushCoalescedCommand(key);
      });
    }

    function scheduleRangeSettleRender(delayMs = 420) {
      const token = ++store.rangeSettleToken;
      setTimeout(async () => {
        if (token !== store.rangeSettleToken) return;
        if (store.nativeRangePointerId !== null || store.activePointerKey) return;
        if (pendingPayloads.size || coalescedTimers.size) return;
        await fetchState();
      }, delayMs);
    }

    function beginNativeRangeInteraction(e) {
      if (!e.isPrimary || (e.pointerType === "mouse" && e.button !== 0)) return;
      const range = e.target instanceof Element ? e.target.closest(NATIVE_RANGE_SELECTOR) : null;
      if (!range || range.disabled || store.nativeRangePointerId !== null) return;
      store.nativeRangePointerId = e.pointerId;
      store.nativeRangeElement = range;
      store.rangeKeyboardUntil = 0;
      range.classList.add("is-dragging");
    }

    function endNativeRangeInteraction(e, interrupted = false) {
      if (store.nativeRangePointerId === null) return;
      if (e && e.pointerId !== store.nativeRangePointerId) return;
      const range = store.nativeRangeElement;
      if (range) {
        range.classList.remove("is-dragging");
        // Work-range inputs also drive the custom-control pointer lock. Clear it
        // when a gesture ends outside the normal inline handler path.
        if (range.classList.contains("work-range") && store.activePointerKey) {
          store.activePointerKey = null;
        }
      }
      store.nativeRangePointerId = null;
      store.nativeRangeElement = null;
      store.rangeSettleUntil = performance.now() + 260;
      flushPendingRangeCommands();
      scheduleRangeSettleRender(interrupted ? 500 : 420);
    }

    function cancelNativeRangeInteraction() {
      if (store.nativeRangePointerId === null) return;
      endNativeRangeInteraction(null, true);
    }

    document.addEventListener("input", e => {
      if (e.target instanceof Element && e.target.matches(NATIVE_RANGE_SELECTOR)) {
        updateRangeFill(e.target);
      }
    }, true);
    document.addEventListener("pointerdown", beginNativeRangeInteraction, true);
    document.addEventListener("pointerup", e => endNativeRangeInteraction(e, false));
    document.addEventListener("pointercancel", e => endNativeRangeInteraction(e, true));
    document.addEventListener("lostpointercapture", e => {
      if (e.pointerId === store.nativeRangePointerId) endNativeRangeInteraction(e, true);
    }, true);
    document.addEventListener("keydown", e => {
      const target = e.target;
      if (!(target instanceof Element) || !target.matches(NATIVE_RANGE_SELECTOR)) return;
      if (["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "PageUp", "PageDown", "Home", "End"].includes(e.key)) {
        store.rangeKeyboardUntil = performance.now() + 1200;
      }
    }, true);
    window.addEventListener("blur", cancelNativeRangeInteraction);
    document.addEventListener("visibilitychange", () => {
      if (document.hidden) cancelNativeRangeInteraction();
    });

    function clamp(v, lo, hi) {
      return Math.max(lo, Math.min(hi, v));
    }

    function faderToDb(f) {
      f = clamp(Number(f) || 0, 0, 1);
      if (f <= 0.001) return -90.0;
      return f <= 0.86 ? (-60.0 + f * (60.0 / 0.86)) : ((f - 0.86) * (10.0 / 0.14));
    }

    function formatFaderDb(f) {
      const db = faderToDb(f);
      if (db <= -59.5) return "-INF";
      if (Math.abs(db) < 0.05) return "0.0 dB";
      return (db >= 0 ? "+" : "") + db.toFixed(1) + " dB";
    }

    // Place scale labels against the same travel curve as the fader cap.
    const FADER_SCALE_MARKS = [
      { db: 10, label: "+10", cls: "" },
      { db: 0, label: "U", cls: "u-mark" },
      { db: -10, label: "-10", cls: "" },
      { db: -20, label: "-20", cls: "" },
      { db: -40, label: "-40", cls: "" },
      { db: -60, label: "-INF", cls: "" }
    ];
    function faderScaleTop(db) {
      const v = db >= 0 ? (0.86 + (db / 10.0) * 0.14) : (0.86 * (1.0 + db / 60.0));
      return ((1.0 - clamp(v, 0, 1)) * 100.0).toFixed(2);
    }

    function gainToDb(g) {
      return (clamp(Number(g) || 0.5, 0, 1) - 0.5) * 24.0;
    }

    function formatGainDb(g) {
      const db = gainToDb(g);
      return (db >= 0 ? "+" : "") + db.toFixed(1) + " dB";
    }

    function formatPan(p) {
      const v = Math.round((clamp(Number(p) || 0.5, 0, 1) - 0.5) * 100);
      if (Math.abs(v) <= 1) return "C";
      return v < 0 ? ("L" + Math.abs(v)) : ("R" + v);
    }

    function formatHz(hz) {
      hz = Number(hz) || 20;
      if (hz >= 1000) return (hz / 1000).toFixed(hz >= 10000 ? 1 : 2) + " kHz";
      return Math.round(hz) + " Hz";
    }

    function normToFreq(norm) {
      return 20.0 * Math.pow(1000.0, clamp(norm, 0, 1));
    }

    function freqToNorm(hz) {
      return clamp(Math.log(clamp(hz, 20, 20000) / 20.0) / Math.log(1000.0), 0, 1);
    }

    function evalPeqBandDb(hz, b, ch) {
      if (!ch || !ch.eq_enabled || !ch.eq_enabled[b]) return 0.0;
      const freq = clamp(ch.eq_freq[b], 20, 20000);
      const q = clamp(ch.eq_q[b], 0.1, 20.0);
      const gain = clamp(ch.eq_gain[b], -18.0, 18.0);
      const type = ch.eq_filter_type ? ch.eq_filter_type[b] : 1;
      hz = Math.max(10.0, hz);
      if (type === 0) {
        const r = hz / freq;
        const slope = Math.max(0.4, q * 1.5);
        return gain / (1.0 + Math.pow(r, 2.0 * slope));
      } else if (type === 2) {
        const r = freq / hz;
        const slope = Math.max(0.4, q * 1.5);
        return gain / (1.0 + Math.pow(r, 2.0 * slope));
      } else if (type === 3) {
        const r6 = Math.pow(freq / hz, 6.0);
        return 10.0 * Math.log10(Math.max(1e-6, 1.0 / (1.0 + r6)));
      } else if (type === 4) {
        const r6 = Math.pow(hz / freq, 6.0);
        return 10.0 * Math.log10(Math.max(1e-6, 1.0 / (1.0 + r6)));
      } else {
        const oct = Math.log2(hz / freq);
        const sigma = (1.0 / Math.max(0.2, q)) / 1.665;
        const z = oct / Math.max(0.01, sigma);
        return gain * Math.exp(-0.5 * z * z);
      }
    }

    function evalPeqResponseDb(hz, ch) {
      if (!ch || ch.eq_bypass) return 0.0;
      let sum = 0.0;
      for (let b = 0; b < 4; b++) sum += evalPeqBandDb(hz, b, ch);
      return clamp(sum, -36.0, 24.0);
    }

    const ISO_FREQS_31 = [
      20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630,
      800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000
    ];

    function evalGeqResponseDb(hz, ch) {
      if (!ch || ch.geq_bypass || ch.eq_bypass || !ch.geq_gains) return 0.0;
      let sum = 0.0;
      for (let i = 0; i < 31; i++) {
        const g = ch.geq_gains[i] || 0;
        if (Math.abs(g) < 0.01) continue;
        const oct = Math.log2(hz / ISO_FREQS_31[i]);
        const z = oct / 0.22;
        sum += g * Math.exp(-0.5 * z * z);
      }
      return clamp(sum, -18.0, 18.0);
    }

    function getSelectedChannelObj() {
      const st = store.state;
      if (!st) return null;
      if (st.selected_aux_bus >= 0 && st.selected_aux_bus < (st.aux_buses || []).length) {
        return st.aux_buses[st.selected_aux_bus];
      }
      if (st.selected_channel === -1) return st.master;
      if (st.selected_channel >= 0 && st.selected_channel < (st.channels || []).length) {
        return st.channels[st.selected_channel];
      }
      return (st.channels && st.channels[0]) || st.master;
    }

    function getTelemetryForId(id) {
      const tel = store.telemetry;
      if (!tel) return null;
      if (id === 0 || id === -1 || id === 100) return tel.master;
      if (id >= 101 && id <= 104 && tel.aux_buses) {
        return tel.aux_buses.find(a => a.id === id) || null;
      }
      if (tel.channels) {
        return tel.channels.find(c => c.id === id) || null;
      }
      return null;
    }

    function getChannelForId(id) {
      const st = store.state;
      if (!st) return null;
      if (Number(id) === 0) return st.master || null;
      return (st.channels || []).find(ch => Number(ch.id) === Number(id)) ||
        (st.aux_buses || []).find(aux => Number(aux.id) === Number(id)) || null;
    }

    const GR_DISPLAY_MAX_DB = 60;

    function isAudioTelemetryRunning() {
      const tel = store.telemetry;
      if (!tel) return false;
      const sys = (store.state && store.state.system) || {};
      const mode = Number(tel.audio_mode ?? sys.audio_mode ?? 0);
      return mode === 0
        ? Boolean(tel.sim_running ?? sys.sim_running)
        : Number(tel.service_state ?? sys.service_state) === 2;
    }

    function parseGainReductionDb(raw) {
      const db = Number(raw);
      if (!Number.isFinite(db) || db > 0.05 || db < -120) return null;
      return Math.min(GR_DISPLAY_MAX_DB, Math.abs(db));
    }

    function getProcessorGrReading(ch, kind) {
      const caps = ch && ch.capabilities ? ch.capabilities : {};
      const supported = kind === "comp" ? Boolean(caps.has_comp)
        : kind === "lim" ? Boolean(caps.has_limiter)
        : Boolean(caps.has_gate);
      if (!supported) return { available: false, valueDb: 0, reason: "PROCESSOR NOT AVAILABLE" };

      const enabled = kind === "comp" ? !ch.comp_bypass
        : kind === "lim" ? Boolean(ch.limiter_enabled)
        : Boolean(ch.gate_on);
      if (!enabled) return { available: false, valueDb: 0, reason: "PROCESSOR BYPASSED" };
      if (!isAudioTelemetryRunning()) return { available: false, valueDb: 0, reason: "ENGINE STOPPED" };

      const tel = getTelemetryForId(ch.id);
      const stateKey = kind === "comp" ? "comp_gr"
        : kind === "lim" ? "limiter_gr_db" : "gate_gr_db";
      const telKey = kind === "comp" ? "comp_gr"
        : kind === "lim" ? "lim_gr" : "gate_gr";
      const raw = tel && tel[telKey] != null ? tel[telKey] : ch[stateKey];
      const valueDb = parseGainReductionDb(raw);
      return valueDb == null
        ? { available: false, valueDb: 0, reason: "TELEMETRY UNAVAILABLE" }
        : { available: true, valueDb, reason: "" };
    }

    function getStripGrReading(ch) {
      const caps = ch && ch.capabilities ? ch.capabilities : {};
      const active = [];
      const missing = [];
      const enabledKinds = [
        ["gate", Boolean(caps.has_gate && ch.gate_on), "GATE"],
        ["comp", Boolean(caps.has_comp && !ch.comp_bypass), "COMP"],
        ["lim", Boolean(caps.has_limiter && ch.limiter_enabled), "LIM"]
      ];

      for (const [kind, enabled, label] of enabledKinds) {
        if (!enabled) continue;
        const reading = getProcessorGrReading(ch, kind);
        if (reading.available) active.push({ label, valueDb: reading.valueDb });
        else missing.push(label);
      }

      const rack = Array.isArray(ch && ch.fx_rack) ? ch.fx_rack : [];
      for (const slot of rack) {
        if (!slot || slot.bypassed || slot.enabled === false) continue;
        const descriptor = `${slot.uri || ""} ${slot.category || ""} ${slot.name || ""}`.toLowerCase();
        const kind = descriptor.includes("compress") ? "comp"
          : descriptor.includes("limit") ? "lim"
          : (descriptor.includes("gate") || descriptor.includes("expander")) ? "gate" : "";
        if (!kind) continue;
        const label = `${kind.toUpperCase()} FX ${Number(slot.slot ?? 0) + 1}`;
        if (!isAudioTelemetryRunning()) {
          missing.push(label);
          continue;
        }
        const raw = kind === "lim" ? slot.limiter_gr_db : slot.gr_db;
        const valueDb = parseGainReductionDb(raw);
        if (valueDb == null) missing.push(label);
        else active.push({ label, valueDb });
      }

      if (!isAudioTelemetryRunning()) {
        return { available: false, valueDb: 0, reason: "ENGINE STOPPED" };
      }
      if (!active.length) {
        return {
          available: false,
          valueDb: 0,
          reason: missing.length ? "TELEMETRY UNAVAILABLE" : "NO ACTIVE DYNAMICS"
        };
      }

      const maximum = active.reduce((best, item) => item.valueDb > best.valueDb ? item : best, active[0]);
      const missingText = missing.length ? `; no telemetry for ${missing.join(", ")}` : "";
      return {
        available: true,
        valueDb: maximum.valueDb,
        source: maximum.label,
        reason: `MAX ACTIVE GR · ${maximum.label} · largest current reduction among enabled dynamics processors${missingText}`
      };
    }

    function updateGrDisplay(root, reading, options = {}) {
      if (!root) return;
      const value = root.querySelector("[data-gr-value]");
      const fill = root.querySelector("[data-gr-fill]");
      const maxDb = Number(options.maxDb || GR_DISPLAY_MAX_DB);
      const available = Boolean(reading && reading.available);
      const valueDb = available ? Math.max(0, Number(reading.valueDb) || 0) : 0;
      root.classList.toggle("unavailable", !available);
      root.classList.toggle("active", available && valueDb > 0.05);
      root.title = available
        ? `${reading.reason} · ${valueDb.toFixed(1)} dB`
        : (reading && reading.reason) || "GAIN REDUCTION UNAVAILABLE";
      root.setAttribute("aria-label", available
        ? `${root.dataset.grLabel || "Gain reduction"}: ${valueDb.toFixed(1)} dB. ${root.title}`
        : `${root.dataset.grLabel || "Gain reduction"} unavailable: ${root.title}`);
      if (value) value.textContent = available ? valueDb.toFixed(1) : "—";
      if (fill) {
        const pct = Math.min(100, (valueDb / maxDb) * 100);
        fill.style.width = available && valueDb > 0.05 ? `max(${pct.toFixed(2)}%, 1px)` : "0";
      }
    }

    function updateWorkGrBadge(el, reading) {
      if (!el) return;
      const available = Boolean(reading && reading.available);
      const valueDb = available ? Math.max(0, Number(reading.valueDb) || 0) : 0;
      el.textContent = available ? valueDb.toFixed(1) : "—";
      el.classList.toggle("hot", available && valueDb > 0.05);
      el.classList.toggle("unavailable", !available);
      el.title = available ? `${reading.reason} · ${valueDb.toFixed(1)} dB` : (reading && reading.reason) || "GAIN REDUCTION UNAVAILABLE";
      el.setAttribute("aria-label", available
        ? `${el.dataset.grLabel || "Gain reduction"}: ${valueDb.toFixed(1)} dB. ${el.title}`
        : `${el.dataset.grLabel || "Gain reduction"} unavailable: ${el.title}`);
    }

    function setupHiDpiCanvas(canvas) {
      if (!canvas) return null;
      const rect = canvas.getBoundingClientRect();
      const w = Math.max(1, Math.floor(rect.width));
      const h = Math.max(1, Math.floor(rect.height));
      const dpr = clamp(window.devicePixelRatio || 1, 1, 3);
      const targetW = Math.round(w * dpr);
      const targetH = Math.round(h * dpr);
      if (canvas.width !== targetW || canvas.height !== targetH) {
        canvas.width = targetW;
        canvas.height = targetH;
      }
      const ctx = canvas.getContext("2d");
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      return { ctx, w, h, dpr };
    }

    // Shared matte display-well treatment for meters, EQ and dynamics surfaces.
    function paintDisplayWell(ctx, w, h) {
      const bg = ctx.createLinearGradient(0, 0, 0, h);
      bg.addColorStop(0, "#0B0C09");
      bg.addColorStop(0.46, "#070806");
      bg.addColorStop(1, "#030403");
      ctx.fillStyle = bg;
      ctx.fillRect(0, 0, w, h);
      ctx.lineWidth = 1;
      ctx.strokeStyle = "rgba(255, 255, 255, 0.032)";
      ctx.strokeRect(0.5, 0.5, Math.max(0, w - 1), Math.max(0, h - 1));
    }

    // High-DPI mono/stereo peak meter. Channel peak values arrive in dBFS.
    function drawStripMeterCanvas(canvas, ch, tel) {
      const env = setupHiDpiCanvas(canvas);
      if (!env || !ch) return;
      const { ctx, w, h } = env;

      const readDb = (telKey, stateKey) => {
        const raw = tel && tel[telKey] != null ? tel[telKey] : ch[stateKey];
        const value = Number(raw);
        return Number.isFinite(value) ? clamp(value, -90, 12) : -90;
      };
      const outL = readDb("out_l", "peak_l");
      const outR = readDb("out_r", "peak_r");
      const holdL = readDb("hold_l", "peak_hold_l");
      const holdR = readDb("hold_r", "peak_hold_r");

      ctx.clearRect(0, 0, w, h);
      paintDisplayWell(ctx, w, h);

      const isStereo = Boolean(ch.format === 1 || ch.capabilities?.is_master || ch.capabilities?.is_aux);
      const top = 2;
      const bottom = Math.max(top + 1, h - 2);
      const trackH = bottom - top;
      const dbToY = db => bottom - clamp((db + 60) / 66, 0, 1) * trackH;
      const barGap = isStereo ? 2 : 0;
      const laneCount = isStereo ? 2 : 1;
      const barW = Math.max(4, Math.floor((w - 4 - barGap * (laneCount - 1)) / laneCount));
      const totalBarW = laneCount * barW + barGap * (laneCount - 1);
      const startX = Math.max(2, Math.floor((w - totalBarW) / 2));
      const gradient = ctx.createLinearGradient(0, bottom, 0, top);
      gradient.addColorStop(0, "#22C55E");
      gradient.addColorStop(clamp((-12 + 60) / 66, 0, 1), "#22C55E");
      gradient.addColorStop(clamp((-12 + 60) / 66, 0, 1), "#F59E0B");
      gradient.addColorStop(clamp((-3 + 60) / 66, 0, 1), "#F59E0B");
      gradient.addColorStop(clamp((-3 + 60) / 66, 0, 1), "#EF4444");
      gradient.addColorStop(1, "#EF4444");

      const drawBar = (x, dbVal, holdDb) => {
        // Recessed lane remains visible at digital silence; signal itself never
        // moves unless the actual peak snapshot rises above the -60 dBFS floor.
        ctx.fillStyle = "#030403";
        ctx.fillRect(x, top, barW, trackH);
        const yTop = dbToY(dbVal);
        if (dbVal > -60 && yTop < bottom) {
          ctx.fillStyle = gradient;
          ctx.fillRect(x, yTop, barW, bottom - yTop);
        }
        if (holdDb > -60) {
          const holdY = clamp(dbToY(holdDb), top, bottom - 2);
          ctx.fillStyle = holdDb >= -0.5 ? "#EF4444" : "#E4E5DF";
          ctx.fillRect(x, holdY, barW, Math.min(2, trackH));
        }
      };

      drawBar(startX, outL, holdL);
      if (isStereo) drawBar(startX + barW + barGap, outR, holdR);

      // dBFS calibration marks live at the edge of the recessed track so they
      // remain legible without obscuring the signal lanes.
      ctx.fillStyle = "rgba(188, 190, 180, 0.42)";
      [-60, -48, -36, -24, -12, -6, 0].forEach(db => {
        const y = clamp(Math.round(dbToY(db)), top, bottom - 1);
        ctx.fillRect(1, y, Math.min(3, Math.max(1, w - 2)), 1);
      });
    }

    // High-DPI Logarithmic EQ + RTA Spectrum Analyzer
    function drawEqAndRtaCanvas(canvas, ch, rta) {
      const env = setupHiDpiCanvas(canvas);
      if (!env || !ch) return;
      const { ctx, w, h } = env;
      const padL = 36, padR = 14, padT = 14, padB = 22;
      const plotW = Math.max(10, w - padL - padR);
      const plotH = Math.max(10, h - padT - padB);

      ctx.clearRect(0, 0, w, h);
      paintDisplayWell(ctx, w, h);

      // Frequency Grid (20 Hz .. 20 kHz)
      const gridFreqs = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
      ctx.font = "10px 'JetBrains Mono', monospace";
      ctx.textAlign = "center";
      gridFreqs.forEach(f => {
        const x = Math.round(padL + freqToNorm(f) * plotW) + 0.5;
        ctx.strokeStyle = (f === 100 || f === 1000 || f === 10000) ? "#30322C" : "#1B1D18";
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(x, padT);
        ctx.lineTo(x, padT + plotH);
        ctx.stroke();

        ctx.fillStyle = "#898C82";
        const lbl = f >= 1000 ? (f / 1000) + "k" : String(f);
        ctx.fillText(lbl, x, h - 6);
      });

      // dB Grid (-18 .. +18 dB)
      const dbLines = [18, 12, 6, 0, -6, -12, -18];
      ctx.textAlign = "right";
      dbLines.forEach(db => {
        const y = Math.round(padT + ((18.0 - db) / 36.0) * plotH) + 0.5;
        ctx.strokeStyle = db === 0 ? "#4B4D46" : "#1B1D18";
        ctx.lineWidth = db === 0 ? 1.2 : 1;
        ctx.beginPath();
        ctx.moveTo(padL, y);
        ctx.lineTo(padL + plotW, y);
        ctx.stroke();

        ctx.fillStyle = db === 0 ? "#E7E9E3" : "#898C82";
        ctx.fillText((db > 0 ? "+" : "") + db, padL - 5, y + 3);
      });

      // RTA Spectrum
      if (rta) {
        const dbToRtaY = db => {
          const norm = clamp((db - (-72.0)) / (12.0 - (-72.0)), 0, 1);
          return padT + plotH - norm * plotH;
        };

        if (rta.mode === 2 && rta.fft_bins && rta.fft_bins.length > 0) {
          const n = rta.fft_bins.length;
          ctx.beginPath();
          ctx.moveTo(padL, padT + plotH);
          for (let i = 0; i < n; i++) {
            const x = padL + (i / (n - 1)) * plotW;
            const y = dbToRtaY(rta.fft_bins[i]);
            ctx.lineTo(x, y);
          }
          ctx.lineTo(padL + plotW, padT + plotH);
          ctx.closePath();
          ctx.fillStyle = "rgba(56, 189, 248, 0.13)";
          ctx.fill();

          ctx.beginPath();
          for (let i = 0; i < n; i++) {
            const x = padL + (i / (n - 1)) * plotW;
            const y = dbToRtaY(rta.fft_bins[i]);
            if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
          }
          ctx.strokeStyle = "rgba(70, 193, 212, 0.66)";
          ctx.lineWidth = 1.5;
          ctx.stroke();

          if (rta.fft_peak && rta.fft_peak.length === n) {
            ctx.beginPath();
            for (let i = 0; i < n; i++) {
              const x = padL + (i / (n - 1)) * plotW;
              const y = dbToRtaY(rta.fft_peak[i]);
              if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
            }
            ctx.strokeStyle = "rgba(193, 197, 188, 0.52)";
            ctx.lineWidth = 1;
            ctx.stroke();
          }
        } else {
          const bands = (rta.mode === 1 && rta.bands_63) ? rta.bands_63 : (rta.bands_31 || []);
          const peaks = (rta.mode === 1 && rta.peak_63) ? rta.peak_63 : (rta.peak_31 || []);
          const count = bands.length;
          if (count > 0) {
            const slotW = plotW / count;
            const barW = Math.max(2, slotW - 2);
            for (let i = 0; i < count; i++) {
              const x = padL + i * slotW + 1;
              const y = dbToRtaY(bands[i]);
              const bh = Math.max(0, (padT + plotH) - y);
              if (bh > 0.5) {
                ctx.fillStyle = "rgba(78, 174, 189, 0.27)";
                ctx.fillRect(x, y, barW, bh);
                ctx.fillStyle = "rgba(83, 204, 221, 0.72)";
                ctx.fillRect(x, y, barW, Math.min(2, bh));
              }
              if (peaks[i] !== undefined && peaks[i] > -71.0) {
                const py = dbToRtaY(peaks[i]);
                ctx.fillStyle = "rgba(210, 214, 206, 0.62)";
                ctx.fillRect(x, py, barW, 1.5);
              }
            }
          }
        }
      }

      // Main EQ Response Curve
      const isGeq = Boolean(ch.geq_mode_active);
      const steps = 160;
      const dbToEqY = db => padT + ((18.0 - clamp(db, -18.0, 18.0)) / 36.0) * plotH;
      const zeroY = dbToEqY(0);

      ctx.beginPath();
      ctx.moveTo(padL, zeroY);
      for (let i = 0; i <= steps; i++) {
        const norm = i / steps;
        const hz = normToFreq(norm);
        const db = isGeq ? evalGeqResponseDb(hz, ch) : evalPeqResponseDb(hz, ch);
        ctx.lineTo(padL + norm * plotW, dbToEqY(db));
      }
      ctx.lineTo(padL + plotW, zeroY);
      ctx.closePath();
      ctx.fillStyle = isGeq ? "rgba(34, 197, 94, 0.13)" : "rgba(228, 229, 223, 0.09)";
      ctx.fill();

      ctx.beginPath();
      for (let i = 0; i <= steps; i++) {
        const norm = i / steps;
        const hz = normToFreq(norm);
        const db = isGeq ? evalGeqResponseDb(hz, ch) : evalPeqResponseDb(hz, ch);
        const x = padL + norm * plotW;
        const y = dbToEqY(db);
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.strokeStyle = ch.eq_bypass ? "#72756C" : (isGeq ? "#2CCB68" : "#F1F4F7");
      ctx.lineWidth = 2.2;
      ctx.stroke();

      // Draggable PEQ Band Handles. Cyan family only; active band is the focus state.
      if (!isGeq && ch.eq_freq && ch.eq_gain) {
        const bandColors = ["#2D4C52", "#32565E", "#376169", "#3C6A73"];
        for (let b = 0; b < 4; b++) {
          const nx = padL + freqToNorm(ch.eq_freq[b]) * plotW;
          const ny = dbToEqY(ch.eq_gain[b]);
          const isAct = (b === store.activeEqBand);
          const enabled = ch.eq_enabled ? ch.eq_enabled[b] : true;

          ctx.beginPath();
          ctx.arc(nx, ny, isAct ? 10 : 8, 0, Math.PI * 2);
          ctx.fillStyle = !enabled ? "#2C2D28" : (isAct ? "#38BDF8" : bandColors[b]);
          ctx.fill();
          ctx.lineWidth = isAct ? 2.5 : 1.5;
          ctx.strokeStyle = isAct ? "#EAFBFF" : (enabled ? "#7BCBD9" : "#4A4C44");
          ctx.stroke();

          ctx.fillStyle = !enabled ? "#7E8177" : (isAct ? "#071012" : "#E4F4F6");
          ctx.font = "bold 9px 'JetBrains Mono', monospace";
          ctx.textAlign = "center";
          ctx.fillText(String(b + 1), nx, ny + 3);
        }
      }
    }

    // Gate & Compressor Curves
    function drawGateCurveCanvas(canvas, ch, tel) {
      const env = setupHiDpiCanvas(canvas);
      if (!env || !ch) return;
      const { ctx, w, h } = env;
      const pad = 26;
      const pw = Math.max(10, w - pad * 2);
      const ph = Math.max(10, h - pad * 2);

      ctx.clearRect(0, 0, w, h);
      paintDisplayWell(ctx, w, h);

      ctx.strokeStyle = "#1A1B19";
      ctx.lineWidth = 1;
      [-60, -40, -20, 0].forEach(db => {
        const norm = (db + 80) / 80;
        const x = pad + norm * pw;
        const y = pad + (1 - norm) * ph;
        ctx.beginPath(); ctx.moveTo(x, pad); ctx.lineTo(x, pad + ph); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(pad, y); ctx.lineTo(pad + pw, y); ctx.stroke();
      });

      ctx.strokeStyle = "#333430";
      ctx.setLineDash([4, 4]);
      ctx.beginPath();
      ctx.moveTo(pad, pad + ph);
      ctx.lineTo(pad + pw, pad);
      ctx.stroke();
      ctx.setLineDash([]);

      ctx.beginPath();
      for (let i = 0; i <= 80; i++) {
        const ptIn = -80 + i;
        let outDb = ptIn;
        if (ch.gate_on) {
          if (ptIn < ch.gate_thresh) outDb = Math.max(-80, ptIn + ch.gate_range);
          else if (ptIn < ch.gate_thresh + 2.0) {
            const t = (ptIn - ch.gate_thresh) / 2.0;
            const closedVal = Math.max(-80, ptIn + ch.gate_range);
            outDb = closedVal + (ptIn - closedVal) * t;
          }
        }
        const x = pad + ((ptIn + 80) / 80) * pw;
        const y = pad + (1 - (clamp(outDb, -80, 0) + 80) / 80) * ph;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.strokeStyle = ch.gate_on ? "#22C55E" : "#6E7069";
      ctx.lineWidth = 2.2;
      ctx.stroke();

      const detDb = clamp(tel ? tel.gate_det : (ch.gate_detector_db || -90), -80, 0);
      if (detDb > -79.5) {
        let outDb = detDb;
        if (ch.gate_on && detDb < ch.gate_thresh) outDb = Math.max(-80, detDb + ch.gate_range);
        const mx = pad + ((detDb + 80) / 80) * pw;
        const my = pad + (1 - (clamp(outDb, -80, 0) + 80) / 80) * ph;
        ctx.beginPath();
        ctx.arc(mx, my, 5, 0, Math.PI * 2);
        ctx.fillStyle = "#F0F4F8";
        ctx.fill();
      }
    }

    function drawCompCurveCanvas(canvas, ch, tel) {
      const env = setupHiDpiCanvas(canvas);
      if (!env || !ch) return;
      const { ctx, w, h } = env;
      const pad = 26;
      const pw = Math.max(10, w - pad * 2);
      const ph = Math.max(10, h - pad * 2);

      ctx.clearRect(0, 0, w, h);
      paintDisplayWell(ctx, w, h);

      ctx.strokeStyle = "#1A1B19";
      ctx.lineWidth = 1;
      [-48, -36, -24, -12, 0].forEach(db => {
        const norm = (db + 60) / 72;
        const x = pad + norm * pw;
        const y = pad + (1 - norm) * ph;
        ctx.beginPath(); ctx.moveTo(x, pad); ctx.lineTo(x, pad + ph); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(pad, y); ctx.lineTo(pad + pw, y); ctx.stroke();
      });

      const evalCompOut = ptIn => {
        let outDb = ptIn;
        if (!ch.comp_bypass) {
          const over = ptIn - ch.comp_thresh;
          const ratio = Math.max(1.0, ch.comp_ratio);
          const knee = Math.max(0.0, ch.comp_knee);
          if (knee <= 0.001) {
            if (over > 0) outDb = ch.comp_thresh + over / ratio;
          } else {
            if (over <= -knee * 0.5) outDb = ptIn;
            else if (over >= knee * 0.5) outDb = ch.comp_thresh + over / ratio;
            else {
              const d = over + knee * 0.5;
              outDb = ptIn + ((1.0 / ratio) - 1.0) * (d * d) / (2.0 * knee);
            }
          }
          outDb += ch.comp_makeup;
        }
        if (ch.limiter_enabled) outDb = Math.min(outDb, ch.limiter_ceiling_db);
        return clamp(outDb, -60, 12);
      };

      ctx.beginPath();
      for (let i = 0; i <= 72; i++) {
        const ptIn = -60 + i;
        const outDb = evalCompOut(ptIn);
        const x = pad + ((ptIn + 60) / 72) * pw;
        const y = pad + (1 - (outDb + 60) / 72) * ph;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.strokeStyle = ch.comp_bypass ? "#6E7069" : "#38BDF8";
      ctx.lineWidth = 2.2;
      ctx.stroke();

      const inDb = clamp(tel ? tel.comp_in : (ch.comp_input || -90), -60, 12);
      if (inDb > -59.5) {
        const outDb = evalCompOut(inDb);
        const mx = pad + ((inDb + 60) / 72) * pw;
        const my = pad + (1 - (outDb + 60) / 72) * ph;
        ctx.beginPath();
        ctx.arc(mx, my, 5, 0, Math.PI * 2);
        ctx.fillStyle = "#F59E0B";
        ctx.fill();
      }
    }

    // Backend Synchronized Appearance / Theme Setup
    function applyBackendTheme(themeObj) {
      if (!themeObj || !themeObj.colors) return;
      const root = document.documentElement;
      const c = themeObj.colors;
      if (c.background) root.style.setProperty("--bg", c.background);
      if (c.panel) root.style.setProperty("--panel", c.panel);
      if (c.control) root.style.setProperty("--control", c.control);
      if (c.recessed) root.style.setProperty("--recessed", c.recessed);
      if (c.border) root.style.setProperty("--border", c.border);
      if (c.text_primary) root.style.setProperty("--text-primary", c.text_primary);
      if (c.text_secondary) root.style.setProperty("--text-secondary", c.text_secondary);
      if (c.selected_border) root.style.setProperty("--selected-border", c.selected_border);
    }

    async function fetchState() {
      if (store.authGateActive || (store.security.configured && !store.security.authenticated)) return;
      const { ok, status, data } = await apiGet("/api/state");
      if (ok && data) {
        store.state = data;
        store.connectionAttempted = true;
        setConnected(true);
        if (store.state.system && store.state.system.theme) {
          applyBackendTheme(store.state.system.theme);
        }
        renderShellStatus();
        renderChannelSelectorStrip();
        if (!shouldDeferLiveRender()) {
          renderActiveView();
        }
        return;
      }
      if (status === 401 || status === 423) return;
      store.connectionAttempted = true;
      setConnected(false);
      renderShellStatus();
      renderActiveView();
    }

    async function fetchCatalog() {
      if (store.authGateActive) return;
      const { ok, data } = await apiGet("/api/catalog");
      if (!ok || !data) return;
      store.catalog = data;
      if (store.modalOpen) renderFxModalBody();
    }

    async function fetchTelemetry() {
      if (document.hidden || store.authGateActive || store.security.locked) return;
      if (store.security.configured && !store.security.authenticated) return;
      const { ok, data } = await apiGet("/api/telemetry");
      if (!ok || !data) return;
      const prevVersion = store.state ? store.state.version : 0;
      store.telemetry = data;
      setConnected(true);
      if (data.version && data.version !== prevVersion && !store.activePointerKey) {
        await fetchState();
      }
      updateLiveVisualizations();
    }

    function setConnected(online) {
      store.connected = online;
      const dot = document.getElementById("netLinkDot");
      const txt = document.getElementById("netLinkText");
      if (dot && txt) {
        dot.className = "status-dot " + (online ? "ok" : "err");
        txt.textContent = online ? "ONLINE" : "OFFLINE";
      }
    }

    function esc(s) {
      return String(s ?? "").replace(/[&<>"']/g, m => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;", "'": "&#39;" }[m]));
    }

    function renderShellStatus() {
      const st = store.state;
      if (!st) {
        const banner = document.getElementById("selectedBanner");
        if (banner) banner.innerHTML = `<span class="tag">—</span><span class="name">NO SELECTION</span>`;

        const dot = document.getElementById("engineStateDot");
        const txt = document.getElementById("engineStateText");
        const btnAudio = document.getElementById("btnQuickAudio");
        const addChannel = document.getElementById("btnAddChannel");
        const status = store.connectionAttempted ? "OFFLINE" : "CONNECTING";
        if (dot) dot.className = "status-dot " + (store.connectionAttempted ? "err" : "warn");
        if (txt) txt.textContent = status;
        if (btnAudio) {
          btnAudio.disabled = true;
          btnAudio.textContent = status;
          btnAudio.className = "hw-btn";
        }
        if (addChannel) addChannel.disabled = true;
        ["telSampleRate", "telCpuLoad", "telXruns"].forEach(id => {
          const el = document.getElementById(id);
          if (el) el.textContent = "—";
        });
        return;
      }
      const sel = getSelectedChannelObj();
      const banner = document.getElementById("selectedBanner");
      if (banner && sel) {
        const isMaster = sel.id === 0;
        const isAux = sel.id >= 101 && sel.id <= 104;
        const ident = getChannelIdentity(sel, isMaster, isAux);
        const chName = isMaster ? "STEREO" : ident.displayName;
        banner.innerHTML = `<span class="tag">${esc(ident.tag)}</span><span class="name">${esc(chName)}</span>`;
      }

      const sys = st.system || {};
      const tel = store.telemetry || {};
      const isSim = (sys.audio_mode === 0);
      const isRunning = isSim ? Boolean(tel.sim_running ?? sys.sim_running) : (sys.service_state === 2);

      const dot = document.getElementById("engineStateDot");
      const txt = document.getElementById("engineStateText");
      const btnAudio = document.getElementById("btnQuickAudio");
      if (dot && txt) {
        dot.className = "status-dot " + (isRunning ? "ok" : (sys.service_state === 4 ? "err" : "warn"));
        txt.textContent = isRunning ? (isSim ? "SIMULATION" : "REAL AUDIO") : "STOPPED";
      }
      if (btnAudio) {
        btnAudio.disabled = false;
        btnAudio.textContent = isRunning ? "STOP AUDIO" : "START AUDIO";
        btnAudio.className = "hw-btn " + (isRunning ? "danger" : "green-on");
      }
      const addChannel = document.getElementById("btnAddChannel");
      if (addChannel) addChannel.disabled = false;

      const srEl = document.getElementById("telSampleRate");
      const cpuEl = document.getElementById("telCpuLoad");
      const xrunEl = document.getElementById("telXruns");
      if (srEl) {
        const srVal = Math.round(sys.actual_sample_rate || 48000);
        srEl.textContent = (srVal >= 1000 ? (srVal / 1000) + " kHz" : srVal + " Hz");
      }
      if (cpuEl) cpuEl.textContent = Number(tel.cpu_load ?? sys.cpu_load ?? 0).toFixed(1) + "%";
      if (xrunEl) xrunEl.textContent = String(tel.underruns ?? sys.underruns ?? 0);
    }

    function renderChannelSelectorStrip() {
      const strip = document.getElementById("channelSelectorStrip");
      const st = store.state;
      if (!strip || !st) return;
      const prevScrollLeft = strip.scrollLeft;
      const prevSelectedId = strip.dataset.selectedId;
      const sel = getSelectedChannelObj();
      const selId = sel ? sel.id : 0;

      let html = "";
      (st.channels || []).forEach(ch => {
        const isSel = (ch.id === selId);
        const t = getTelemetryForId(ch.id);
        const clip = t ? t.clip : ch.clip;
        const activeSig = (t ? t.out_l : ch.peak_l) > -55;
        const dotCls = clip ? "err" : (activeSig ? "ok" : "");
        const ident = getChannelIdentity(ch, false, false);
        html += `<button type="button" class="ch-pill ${isSel ? "selected" : ""}" onclick="window.prismix.selectChannel(${ch.id})">
          <span class="status-dot ${dotCls}"></span>
          <span>${ident.shortTag}</span>
          <span style="color:var(--text-primary);font-weight:700;">${esc(ident.displayName)}</span>
        </button>`;
      });

      html += `<div class="ch-sep"></div>`;
      (st.aux_buses || []).forEach(aux => {
        const isSel = (aux.id === selId);
        html += `<button type="button" class="ch-pill aux ${isSel ? "selected" : ""}" onclick="window.prismix.selectChannel(${aux.id})">
          <span>${esc(aux.name)}</span>
        </button>`;
      });

      html += `<div class="ch-sep"></div>`;
      const mSel = (selId === 0);
      html += `<button type="button" class="ch-pill master ${mSel ? "selected" : ""}" onclick="window.prismix.selectChannel(0)">
        <span>MAIN L/R</span>
      </button>`;

      strip.innerHTML = html;
      strip.scrollLeft = prevScrollLeft;
      strip.dataset.selectedId = String(selId);

      // Keep the selector's selected pill in view when selection changes,
      // without fighting a manual horizontal swipe on telemetry updates.
      const alignPill = () => {
        const selected = strip.querySelector(".ch-pill.selected");
        if (!selected) return;
        if (selId === 0) {
          strip.scrollLeft = strip.scrollWidth;
        } else {
          const left = selected.offsetLeft;
          const width = selected.offsetWidth;
          const stripWidth = strip.clientWidth;
          if (stripWidth > 0 && width > 0) {
            strip.scrollLeft = Math.max(0, left - (stripWidth - width) / 2);
          } else {
            selected.scrollIntoView({ block: "nearest", inline: "center" });
          }
        }
      };
      if (prevSelectedId !== String(selId)) {
        alignPill();
        requestAnimationFrame(alignPill);
      }
    }

    // Compact strip identity + naming (avoid repeating the same channel ID twice)
    function getChannelIdentity(ch, isMaster = false, isAux = false) {
      const inputNo = String(ch.id).padStart(2, "0");
      const auxNo = String(ch.id - 100).padStart(2, "0");
      const tag = isMaster ? "MAIN L/R" : (isAux ? ("AUX " + auxNo) : ("CH " + inputNo));
      const shortTag = isMaster ? "M" : (isAux ? ("A" + auxNo) : inputNo);
      const typeTag = isMaster ? "MASTER" : (isAux ? "ST" : (ch.format === 1 ? "ST" : "MONO"));
      const raw = String(ch.name || "").trim();
      let generic = false;
      if (!isMaster && raw) {
        if (isAux) {
          generic = new RegExp("^AUX\\s*0?" + (ch.id - 100) + "$", "i").test(raw);
        } else {
          generic = new RegExp("^(CH|CHANNEL|INPUT)\\s*0?" + ch.id + "$", "i").test(raw);
        }
      }
      const fallback = isMaster ? "MAIN L/R" : (isAux ? "RETURN" : "INPUT");
      const displayName = (!raw || generic) ? fallback : raw;
      const fullName = (raw && !generic) ? (tag + " · " + raw) : tag;
      return { tag, shortTag, typeTag, displayName, fullName };
    }

    // Channel Strip Module Builder
    function buildStripCardHtml(ch, isMaster = false, isAux = false) {
      const sel = getSelectedChannelObj();
      const isSelected = Boolean(sel && sel.id === ch.id);
      const caps = ch.capabilities || {};
      const ident = getChannelIdentity(ch, isMaster, isAux);
      const fxCount = Array.isArray(ch.fx_rack) ? ch.fx_rack.length : Number(ch.fx_rack_count || 0);
      const sourceLabel = ch.input_label || (isMaster ? "MAIN OUTPUT" : (isAux ? "AUX RETURN" : "UNASSIGNED"));
      const gainValue = isAux ? (ch.return_gain ?? 0.86) : (ch.gain ?? 0.5);
      const gainDefault = isAux ? 0.86 : 0.5;
      const gainLabel = isAux ? "RETURN" : "GAIN";
      const panLabel = isMaster || isAux || ch.format === 1 ? "BAL" : "PAN";
      const grReading = getStripGrReading(ch);
      const grValue = grReading.available ? grReading.valueDb.toFixed(1) : "—";
      const grWidth = grReading.available && grReading.valueDb > 0.05
        ? `max(${Math.min(100, grReading.valueDb / GR_DISPLAY_MAX_DB * 100).toFixed(2)}%, 1px)` : "0";

      const proc = [];
      if (caps.has_hpf) proc.push({ label: "HPF", on: Boolean(ch.hpf_on), view: 1, title: "High-pass filter" });
      if (caps.has_gate) proc.push({ label: "GATE", on: Boolean(ch.gate_on), view: 3, title: "Noise gate" });
      if (caps.has_comp) proc.push({ label: "COMP", on: !ch.comp_bypass, view: 3, title: "Compressor" });
      if (caps.has_peq) proc.push({ label: ch.geq_mode_active ? "GEQ" : "PEQ", on: !ch.eq_bypass, view: 2, title: "Equalizer" });
      if (caps.has_limiter) proc.push({ label: "LIM", on: Boolean(ch.limiter_enabled), view: 3, title: "Limiter" });
      if (caps.has_fx_rack) proc.push({ label: fxCount > 0 ? ("FX" + fxCount) : "FX", on: fxCount > 0, view: 4, title: "Insert FX rack" });

      const badgesHtml = proc.map(p => `<button type="button" class="proc-badge ${p.on ? "on" : ""}"
        title="${esc(p.title)} — ${p.on ? "active" : "bypassed"}; open processor"
        onclick="window.prismix.jumpToProc(${ch.id},${p.view})">${esc(p.label)}</button>`).join("");

      const selectLabel = "Select " + ident.fullName;
      return `<div class="channel-strip ${isSelected ? "selected" : ""} ${isMaster ? "master-strip" : ""} ${isAux ? "aux-strip" : ""}" data-strip-id="${ch.id}">
        <button type="button" class="strip-head" onclick="window.prismix.selectChannel(${ch.id})"
                title="${esc(ident.fullName)}" aria-label="${esc(selectLabel)}">
          <span class="strip-head-row">
            <span class="strip-num-badge">${esc(ident.shortTag)}</span>
            <span class="strip-format-tag">${esc(ident.typeTag)}</span>
          </span>
          <span class="strip-name" title="${esc(ident.fullName)}">${esc(ident.displayName)}</span>
        </button>

        <div class="strip-source-row">
          <button type="button" class="strip-source-label" title="Edit input and channel settings"
                  onclick="window.prismix.setView(1)">${esc(sourceLabel)}</button>
          ${(!isMaster && !isAux) ? `<div class="strip-source-actions">
            <button type="button" class="hw-btn strip-source-action" title="Rename channel"
                    onclick="window.prismix.promptRenameChannel(${ch.id})">REN</button>
            <button type="button" class="hw-btn danger strip-source-action" title="Delete channel"
                    onclick="window.prismix.deleteChannel(${ch.id})">DEL</button>
          </div>` : ""}
        </div>

        <div class="strip-badges">${badgesHtml}</div>

        <div class="fader-meter-bay">
          <div class="v-fader-track" tabindex="0" role="slider" aria-label="${esc(ident.fullName)} fader"
               aria-valuemin="0" aria-valuemax="100" aria-valuenow="${Math.round(ch.fader * 100)}"
               data-fader="strip" data-ch="${ch.id}" data-val="${ch.fader}" data-default="0.86">
            <div class="v-fader-scale">
              ${FADER_SCALE_MARKS.map(mark =>
                `<span class="${mark.cls}" style="top:${faderScaleTop(mark.db)}%">${mark.label}</span>`
              ).join("")}
            </div>
            <div class="v-fader-slot"></div>
            <div class="v-fader-unity"></div>
            <div class="v-fader-cap" style="top:calc(14px + ${(1 - clamp(ch.fader, 0, 1)).toFixed(4)} * (100% - 28px))"></div>
          </div>
          <div class="meter-column">
            <div class="clip-led ${ch.clip ? "hot" : ""}" data-clip-led="${ch.id}">CLIP</div>
            <canvas class="strip-meter-canvas" data-meter-canvas="${ch.id}"
                    aria-label="${esc(ident.fullName)} ${ch.format === 1 ? "stereo" : "mono"} peak meter, dBFS"></canvas>
            <div class="strip-gr-meter ${grReading.available ? (grReading.valueDb > 0.05 ? "active" : "") : "unavailable"}"
                 data-strip-gr="${ch.id}" data-gr-label="${esc(ident.fullName + " gain reduction")}" title="${esc(grReading.reason)}">
              <div class="strip-gr-meta"><span>GR</span><span class="strip-gr-value" data-gr-value>${grValue}</span></div>
              <div class="strip-gr-track"><span class="strip-gr-fill" data-gr-fill style="width:${grWidth}"></span></div>
            </div>
            <div class="strip-gain-control">
              <div class="rotary-dial strip-knob" tabindex="0" role="slider"
                   aria-label="${esc(ident.fullName)} ${gainLabel.toLowerCase()}"
                   data-rotary="gain" data-ch="${ch.id}" data-val="${gainValue}" data-default="${gainDefault}">
                <div class="rotary-pointer" style="transform:rotate(${((gainValue * 270) - 135).toFixed(1)}deg)"></div>
              </div>
              <span class="strip-knob-label">${gainLabel}</span>
              <span class="strip-knob-value" data-gain-lbl="${ch.id}">${isAux ? formatFaderDb(gainValue) : formatGainDb(gainValue)}</span>
            </div>
          </div>
        </div>

        <div class="strip-db-readout" data-fader-lbl="${ch.id}">${formatFaderDb(ch.fader)}</div>

        <div class="strip-pan-control">
          <div class="rotary-dial strip-knob" tabindex="0" role="slider"
               aria-label="${esc(ident.fullName)} ${panLabel.toLowerCase()}"
               data-rotary="pan" data-ch="${ch.id}" data-val="${ch.pan}" data-default="0.5">
            <div class="rotary-pointer" style="transform:rotate(${((ch.pan * 270) - 135).toFixed(1)}deg)"></div>
          </div>
          <span class="strip-knob-label">${panLabel}</span>
          <span class="strip-knob-value" data-pan-lbl="${ch.id}">${formatPan(ch.pan)}</span>
        </div>

        <div class="strip-btns">
          <button type="button" class="hw-btn mute-btn ${ch.mute ? "on" : ""}" aria-pressed="${Boolean(ch.mute)}"
                  title="Mute ${esc(ident.fullName)}" onclick="window.prismix.toggleMute(${ch.id}, ${!ch.mute})">MUTE</button>
          ${isMaster ? `<button type="button" class="hw-btn solo-btn strip-master-solo ${ch.solo ? "on" : ""}"
                       aria-pressed="${Boolean(ch.solo)}" title="Solo ${esc(ident.fullName)}"
                       onclick="window.prismix.toggleSolo(${ch.id}, ${!ch.solo})">SOLO</button>
            <button type="button" class="hw-btn strip-limiter-btn ${ch.limiter_enabled ? "active" : ""}" aria-pressed="${Boolean(ch.limiter_enabled)}"
                       title="Toggle master limiter" onclick="window.prismix.cmd({action:'set_limiter_enabled',channel:0,value:${!ch.limiter_enabled}})">LIM</button>`
            : `<button type="button" class="hw-btn solo-btn ${ch.solo ? "on" : ""}" aria-pressed="${Boolean(ch.solo)}"
                       title="Solo ${esc(ident.fullName)}" onclick="window.prismix.toggleSolo(${ch.id}, ${!ch.solo})">SOLO</button>`}
          <button type="button" class="hw-btn strip-select-btn ${isSelected ? "active" : ""}"
                  aria-pressed="${isSelected}" title="Select ${esc(ident.fullName)}"
                  onclick="window.prismix.selectChannel(${ch.id})">SELECT</button>
          ${isMaster ? `<button type="button" class="hw-btn strip-add-btn"
                       title="Add input channel" onclick="window.prismix.addChannel()">+ ADD CH</button>` : ""}
        </div>
      </div>`;
    }

    function buildWorkChannelModule(st, ch, isMaster, isAux, ident) {
      const caps = ch.capabilities || {};
      const inputLabel = ch.input_label || (isMaster ? "STEREO BUS" : (isAux ? "AUX RETURN" : "UNASSIGNED"));

      let body = "";
      if (isMaster) {
        const gainValue = ch.gain ?? 0.5;
        const gainLabel = formatGainDb(gainValue);
        const ceiling = Number(ch.limiter_ceiling_db ?? -0.5);
        const release = Number(ch.limiter_release_ms ?? 100);
        body += `<div class="work-knobs">
          <div class="work-knob">
            <div class="rotary-dial" tabindex="0" role="slider" aria-label="Main bus gain"
                 data-rotary="gain" data-ch="${ch.id}" data-val="${gainValue}" data-default="0.5">
              <div class="rotary-pointer" style="transform:rotate(${((gainValue * 270) - 135).toFixed(1)}deg)"></div>
            </div>
            <span class="work-knob-lbl">GAIN</span>
            <span class="work-knob-val" data-gain-lbl="${ch.id}">${gainLabel}</span>
          </div>
          <div class="work-knob">
            <div class="rotary-dial" tabindex="0" role="slider" aria-label="Main bus pan"
                 data-rotary="pan" data-ch="${ch.id}" data-val="${ch.pan}" data-default="0.5">
              <div class="rotary-pointer" style="transform:rotate(${((ch.pan * 270) - 135).toFixed(1)}deg)"></div>
            </div>
            <span class="work-knob-lbl">PAN</span>
            <span class="work-knob-val" data-pan-lbl="${ch.id}">${formatPan(ch.pan)}</span>
          </div>
        </div>`;
        body += `<div class="work-control-stack">
          ${caps.has_limiter ? `
          <div class="work-control-line">
            <div class="work-control-head"><span>LIMIT CEILING</span><b id="workLimCeil">${ceiling.toFixed(1)} dBFS</b></div>
            <input type="range" class="work-range" min="-18" max="0" step="0.1" value="${ceiling}"
                   aria-label="Limiter ceiling"
                   oninput="document.getElementById('workLimCeil').textContent=Number(this.value).toFixed(1)+' dBFS';window.prismix.onLimParam(${ch.id},'ceiling',this.value)"
                   onpointerdown="window.prismix.beginWorkControl('lim_${ch.id}_ceiling')"
                   onpointerup="window.prismix.endWorkControl('lim_${ch.id}_ceiling')"
                   onchange="window.prismix.endWorkControl('lim_${ch.id}_ceiling')">
          </div>
          <div class="work-control-line">
            <div class="work-control-head"><span>RELEASE</span><b id="workLimRel">${release.toFixed(0)} ms</b></div>
            <input type="range" class="work-range" min="10" max="1000" step="10" value="${release}"
                   aria-label="Limiter release"
                   oninput="document.getElementById('workLimRel').textContent=Number(this.value).toFixed(0)+' ms';window.prismix.onLimParam(${ch.id},'release',this.value)"
                   onpointerdown="window.prismix.beginWorkControl('lim_${ch.id}_release')"
                   onpointerup="window.prismix.endWorkControl('lim_${ch.id}_release')"
                   onchange="window.prismix.endWorkControl('lim_${ch.id}_release')">
          </div>` : `<div class="work-module-note">Limiter unavailable</div>`}
        </div>`;
      } else {
        const gainValue = isAux ? (ch.return_gain ?? 0.86) : (ch.gain ?? 0.5);
        const gainLabel = isAux ? formatFaderDb(gainValue) : formatGainDb(gainValue);
        const knobLabel = isAux ? "RETURN" : "GAIN";
        body += `<div class="work-knobs">
          <div class="work-knob">
            <div class="rotary-dial" tabindex="0" role="slider" aria-label="${esc(ident.fullName)} ${knobLabel.toLowerCase()}"
                 data-rotary="gain" data-ch="${ch.id}" data-val="${gainValue}" data-default="${isAux ? 0.86 : 0.5}">
              <div class="rotary-pointer" style="transform:rotate(${((gainValue * 270) - 135).toFixed(1)}deg)"></div>
            </div>
            <span class="work-knob-lbl">${knobLabel}</span>
            <span class="work-knob-val" data-gain-lbl="${ch.id}">${gainLabel}</span>
          </div>
          <div class="work-knob">
            <div class="rotary-dial" tabindex="0" role="slider" aria-label="${esc(ident.fullName)} pan"
                 data-rotary="pan" data-ch="${ch.id}" data-val="${ch.pan}" data-default="0.5">
              <div class="rotary-pointer" style="transform:rotate(${((ch.pan * 270) - 135).toFixed(1)}deg)"></div>
            </div>
            <span class="work-knob-lbl">PAN</span>
            <span class="work-knob-val" data-pan-lbl="${ch.id}">${formatPan(ch.pan)}</span>
          </div>
        </div>`;

        if (caps.has_preamp) {
          body += `<div class="work-switch-row">
            <button type="button" class="hw-btn ${ch.phantom_on ? "mute-btn on" : ""}" aria-pressed="${Boolean(ch.phantom_on)}"
                    title="Toggle +48V phantom power" onclick="window.prismix.cmd({action:'set_phantom',channel:${ch.id},value:${!ch.phantom_on}})">+48V</button>
            <button type="button" class="hw-btn ${ch.polarity_inverted ? "solo-btn on" : ""}" aria-pressed="${Boolean(ch.polarity_inverted)}"
                    title="Invert input polarity" onclick="window.prismix.cmd({action:'set_polarity',channel:${ch.id},value:${!ch.polarity_inverted}})">Ø PHASE</button>
          </div>`;
        }

        if (caps.has_fx_rack) {
          const rack = Array.isArray(ch.fx_rack) ? ch.fx_rack : [];
          const first = rack[0];
          const fxText = first
            ? ("SLOT " + (Number(first.slot ?? 0) + 1) + " · " + first.name + (rack.length > 1 ? " +" + (rack.length - 1) : ""))
            : "NO INSERTS";
          body += `<div class="work-inline">
            <span class="work-inline-label">INSERT FX</span>
            <button type="button" class="work-insert-btn" onclick="window.prismix.setView(4)" title="Open insert FX rack">
              <span>${esc(fxText)}</span><span>›</span>
            </button>
          </div>`;
        }
      }

      return `<section class="work-module work-channel" aria-label="Selected channel settings">
        <div class="work-module-head">
          <span class="work-module-title">${isMaster ? "MASTER" : "CHANNEL"}</span>
          <span class="work-module-note" title="${esc(inputLabel)}">${esc(inputLabel)}</span>
        </div>
        ${body}
      </section>`;
    }

    function buildWorkGrBadge(ch, kind, title) {
      const reading = getProcessorGrReading(ch, kind);
      return `<span class="work-gr ${reading.available && reading.valueDb > 0.05 ? "hot" : ""} ${reading.available ? "" : "unavailable"}"
                   data-work-gr="${kind}" data-work-gr-channel="${ch.id}"
                   data-gr-label="${esc(title + " gain reduction")}" title="${esc(reading.reason)}">
                ${reading.available ? reading.valueDb.toFixed(1) : "—"}</span>`;
    }

    function buildDynamicsGrDisplay(ch, kind, label) {
      const reading = getProcessorGrReading(ch, kind);
      const pct = reading.available && reading.valueDb > 0.05
        ? `max(${Math.min(100, reading.valueDb / GR_DISPLAY_MAX_DB * 100).toFixed(2)}%, 1px)` : "0";
      return `<div class="dyn-gr-readout ${reading.available && reading.valueDb > 0.05 ? "active" : ""} ${reading.available ? "" : "unavailable"}"
                   data-dyn-gr="${kind}" data-dyn-gr-channel="${ch.id}"
                   data-gr-label="${esc(label + " gain reduction")}" title="${esc(reading.reason)}">
                <span class="dyn-gr-label">${esc(label)} GR</span>
                <div class="dyn-gr-track"><span class="dyn-gr-fill" data-gr-fill style="width:${pct}"></span></div>
                <span class="dyn-gr-value" data-gr-value>${reading.available ? reading.valueDb.toFixed(1) + " dB" : "—"}</span>
              </div>`;
    }

    function buildWorkProcessingModule(ch, isMaster, isAux, ident) {
      const caps = ch.capabilities || {};
      const rows = [];

      const procRow = (label, on, value, action, grKind, title) => `<div class="work-proc-row">
        <button type="button" class="work-toggle ${on ? "on" : ""}" aria-pressed="${Boolean(on)}"
                title="${esc(title)} — ${on ? "active" : "bypassed"}" onclick="${action}">${esc(label)}</button>
        <span class="work-proc-val" title="${esc(title)}">${esc(value)}</span>
        ${grKind ? buildWorkGrBadge(ch, grKind, title) : ""}
      </div>`;

      if (caps.has_hpf) {
        rows.push(procRow("HPF", Boolean(ch.hpf_on), Math.round(ch.hpf_frequency || 80) + " Hz",
          `window.prismix.cmd({action:'set_hpf_enabled',channel:${ch.id},value:${!ch.hpf_on}})`, "", "High-pass filter"));
      }
      if (caps.has_gate) {
        rows.push(procRow("GATE", Boolean(ch.gate_on), "THR " + Number(ch.gate_thresh ?? -48).toFixed(0) + " dB",
          `window.prismix.cmd({action:'set_gate_enabled',channel:${ch.id},value:${!ch.gate_on}})`, "gate", "Noise gate"));
      }
      if (caps.has_comp) {
        rows.push(procRow("COMP", !ch.comp_bypass, "THR " + Number(ch.comp_thresh ?? -18).toFixed(0) + " / " + Number(ch.comp_ratio ?? 4).toFixed(1) + ":1",
          `window.prismix.cmd({action:'set_comp_bypass',channel:${ch.id},value:${!ch.comp_bypass}})`, "comp", "Compressor"));
      }
      if (caps.has_peq) {
        rows.push(procRow("EQ", !ch.eq_bypass, ch.geq_mode_active ? "31-BAND GEQ" : "4-BAND PEQ",
          `window.prismix.cmd({action:'set_eq_bypass',channel:${ch.id},value:${!ch.eq_bypass}})`, "", "Equalizer"));
      }
      if (caps.has_limiter) {
        rows.push(procRow("LIM", Boolean(ch.limiter_enabled), "CEIL " + Number(ch.limiter_ceiling_db ?? -0.5).toFixed(1) + " dB",
          `window.prismix.cmd({action:'set_limiter_enabled',channel:${ch.id},value:${!ch.limiter_enabled}})`, "lim", "Limiter"));
      }

      const hasDynamics = caps.has_gate || caps.has_comp || caps.has_limiter;
      const detailView = hasDynamics ? 3 : 2;
      const detailLabel = hasDynamics ? "DYN" : "EQ";
      return `<section class="work-module work-processing" aria-label="Selected channel processing">
        <div class="work-module-head">
          <span class="work-module-title">PROCESSING</span>
          <button type="button" class="work-link" onclick="window.prismix.setView(${detailView})" title="Open detailed processor controls">${detailLabel} ›</button>
        </div>
        ${rows.length ? `<div class="work-proc-list">${rows.join("")}</div>` : `<span class="work-module-note">NO PROCESSING</span>`}
      </section>`;
    }

    function buildWorkEqModule(ch, ident) {
      const eqState = ch.eq_bypass ? "BYPASS" : "ACTIVE";
      const eqMode = ch.geq_mode_active ? "31-BAND GEQ" : "4-BAND PEQ";
      return `<section class="work-module work-eq" aria-label="Selected channel EQ and RTA">
        <div class="work-module-head">
          <span class="work-module-title">EQ + RTA</span>
          <span class="work-module-note">${esc(eqMode)} · ${eqState}</span>
          <button type="button" class="work-link" onclick="window.prismix.setView(2)" title="Open EQ and RTA editor">OPEN ›</button>
        </div>
        <div class="work-eq-wrap" onclick="window.prismix.setView(2)" title="Open EQ and RTA editor">
          <canvas class="work-eq-canvas" id="mixerEqPreview" role="img"
                  aria-label="${esc(ident.fullName)} EQ response and live RTA"></canvas>
        </div>
      </section>`;
    }

    function buildWorkRoutingModule(st, ch, isMaster, isAux, ident) {
      const caps = ch.capabilities || {};
      let body = "";

      if (caps.has_aux_sends) {
        body += `<div class="work-sends">${[0, 1, 2, 3].map(a => {
          const lvl = ch.sends ? (ch.sends[a] ?? 0) : 0;
          const en = ch.send_enabled ? (ch.send_enabled[a] ?? true) : true;
          const auxName = (st.aux_buses && st.aux_buses[a] && st.aux_buses[a].name) || ("AUX " + (a + 1));
          const key = "send_" + ch.id + "_" + a;
          return `<div class="work-send-row">
            <span class="work-send-lbl" title="${esc(auxName)}">A${a + 1}</span>
            <input type="range" class="work-range" min="0" max="1" step="0.01" value="${lvl}"
                   aria-label="${esc(ident.fullName)} send to ${esc(auxName)}"
                   oninput="document.getElementById('workSendVal_${ch.id}_${a}').textContent=Math.round(this.value*100)+'%';window.prismix.coalesce('${key}',{action:'set_aux_send',channel:${ch.id},aux:${a},value:Number(this.value)})"
                   onpointerdown="window.prismix.beginWorkControl('${key}')"
                   onpointerup="window.prismix.endWorkControl('${key}')"
                   onchange="window.prismix.endWorkControl('${key}')">
            <span class="work-send-val" id="workSendVal_${ch.id}_${a}">${Math.round(lvl * 100)}%</span>
            <button type="button" class="work-send-en ${en ? "on" : ""}" aria-pressed="${Boolean(en)}"
                    title="${en ? "Mute" : "Enable"} ${esc(auxName)} send"
                    onclick="window.prismix.cmd({action:'set_aux_send_enabled',channel:${ch.id},aux:${a},value:${!en}})">${en ? "ON" : "OFF"}</button>
          </div>`;
        }).join("")}</div>`;
      }

      if (isMaster) {
        body += `<div class="work-route-stack">
          <div class="work-route-line">
            <span class="work-route-lbl">MAIN OUTPUT</span>
            <span class="work-module-note">OUT 1 / 2</span>
          </div>`;
        if (caps.has_speaker_delay) {
          const delayText = ch.speaker_delay_enabled
            ? (Number(ch.speaker_delay_l_ms || 0).toFixed(1) + " / " + Number(ch.speaker_delay_r_ms || 0).toFixed(1) + " ms")
            : "BYPASSED";
          body += `<div class="work-route-line">
            <span class="work-route-lbl">SPEAKER DELAY</span>
            <button type="button" class="work-route-btn ${ch.speaker_delay_enabled ? "on" : ""}"
                    aria-pressed="${Boolean(ch.speaker_delay_enabled)}" title="Toggle speaker alignment delay"
                    onclick="window.prismix.cmd({action:'set_speaker_delay',enabled:${!ch.speaker_delay_enabled}})">${esc(delayText)}</button>
          </div>`;
        }
        body += `<div class="work-route-line">
            <span class="work-route-lbl">LIMITER GR</span>
            ${buildWorkGrBadge(ch, "lim", "Limiter")}
          </div>
        </div>`;
      } else {
        const routeText = ch.route_to_main ? "ASSIGNED" : "UNASSIGNED";
        body += `<div class="work-route-stack">
          <div class="work-route-line">
            <span class="work-route-lbl">MAIN L/R</span>
            <button type="button" class="work-route-btn ${ch.route_to_main ? "on" : ""}"
                    aria-pressed="${Boolean(ch.route_to_main)}" title="Toggle assignment to Main L/R"
                    onclick="window.prismix.cmd({action:'set_output_route',channel:${ch.id},route_to_main:${!ch.route_to_main}})">${routeText}</button>
          </div>`;
        if (isAux && ch.input_label) {
          body += `<div class="work-route-line">
            <span class="work-route-lbl">RETURN PATH</span>
            <span class="work-module-note" title="${esc(ch.input_label)}">${esc(ch.input_label)}</span>
          </div>`;
        }
        body += `</div>`;
      }

      return `<section class="work-module work-routing" aria-label="Selected channel routing and sends">
        <div class="work-module-head">
          <span class="work-module-title">ROUTING / SENDS</span>
          <button type="button" class="work-link" onclick="window.prismix.setView(5)" title="Open routing matrix">ROUTE ›</button>
        </div>
        ${body}
      </section>`;
    }

    function buildSelectedWorkspaceHtml(st, ch) {
      const caps = ch.capabilities || {};
      const isMaster = Boolean(caps.is_master || ch.id === 0);
      const isAux = Boolean(caps.is_aux || (ch.id >= 101 && ch.id <= 104));
      const ident = getChannelIdentity(ch, isMaster, isAux);
      const typeText = isMaster ? "STEREO MASTER" : (isAux ? "STEREO" : (ch.format === 1 ? "STEREO" : "MONO"));
      const routeText = isMaster ? "MAIN OUT" : (ch.route_to_main ? "MAIN L/R" : "UNROUTED");
      const patchText = ch.input_label || "UNASSIGNED";
      const faderLabel = formatFaderDb(ch.fader);
      const panLabel = formatPan(ch.pan);

      return `<section class="mixer-workspace" data-work-id="${ch.id}" aria-label="Selected channel workspace">
        <div class="work-head">
          <div class="work-ident">
            <span class="work-tag">${esc(ident.tag)}</span>
            ${ident.displayName !== ident.tag ? `<span class="work-name" title="${esc(ident.fullName)}">${esc(ident.displayName)}</span>` : ""}
            <span class="work-type">${esc(typeText)}</span>
          </div>
          <div class="work-meta">
            <span class="work-meta-item"><span>FADER</span><b data-work-fader-lbl="${ch.id}">${faderLabel}</b></span>
            <span class="work-meta-item"><span>PAN</span><b data-work-pan-lbl="${ch.id}">${panLabel}</b></span>
            <span class="work-meta-item"><span>PATCH</span><b title="${esc(patchText)}">${esc(patchText)}</b></span>
            <span class="work-meta-item"><span>ROUTE</span><b>${esc(routeText)}</b></span>
          </div>
          <div class="work-actions">
            <button type="button" class="hw-btn mute-btn ${ch.mute ? "on" : ""}" aria-pressed="${Boolean(ch.mute)}"
                    onclick="window.prismix.toggleMute(${ch.id}, ${!ch.mute})">MUTE</button>
            ${isMaster ? "" : `<button type="button" class="hw-btn solo-btn ${ch.solo ? "on" : ""}" aria-pressed="${Boolean(ch.solo)}"
                    onclick="window.prismix.toggleSolo(${ch.id}, ${!ch.solo})">SOLO</button>`}
            <button type="button" class="hw-btn" onclick="window.prismix.setView(1)">DETAIL</button>
          </div>
        </div>
        <div class="work-body">
          ${buildWorkChannelModule(st, ch, isMaster, isAux, ident)}
          ${buildWorkProcessingModule(ch, isMaster, isAux, ident)}
          ${buildWorkEqModule(ch, ident)}
          ${buildWorkRoutingModule(st, ch, isMaster, isAux, ident)}
        </div>
      </section>`;
    }

    // Keep the docked master rail aligned with the channel strips when the
    // channel bank reserves space for its horizontal scrollbar.
    function syncMixerDeckMetrics() {
      const deck = document.querySelector(".mixer-deck");
      const bank = deck ? deck.querySelector(".mixer-bank") : null;
      if (!deck || !bank) return;
      const rail = bank.querySelector(".strip-rail");
      const master = deck.querySelector(".mixer-master-bay");
      const view = deck.closest(".mixer-view");
      if (rail && master && window.innerWidth >= 901) {
        const bankStyle = window.getComputedStyle(bank);
        const bankChrome = ["paddingLeft", "paddingRight", "borderLeftWidth", "borderRightWidth"]
          .reduce((total, property) => total + (parseFloat(bankStyle[property]) || 0), 0);
        const naturalConsoleWidth = rail.scrollWidth + bankChrome + master.offsetWidth;
        const availableWidth = deck.parentElement ? deck.parentElement.clientWidth : window.innerWidth;
        const needsHorizontalBankScroll = naturalConsoleWidth > availableWidth;
        const canUseSideWorkspace = !needsHorizontalBankScroll &&
          (availableWidth - naturalConsoleWidth) >= 320;
        const hasNarrowWorkspace = canUseSideWorkspace &&
          (availableWidth - naturalConsoleWidth - 8) < 650;
        deck.classList.toggle("is-content-sized", !needsHorizontalBankScroll);
        if (view) {
          view.classList.toggle("has-workspace-rail", canUseSideWorkspace);
          view.classList.toggle("has-workspace-under", !needsHorizontalBankScroll && !canUseSideWorkspace);
          view.classList.toggle("has-dense-bank", needsHorizontalBankScroll);
          view.classList.toggle("has-narrow-workspace", hasNarrowWorkspace);
        }
      } else {
        deck.classList.remove("is-content-sized");
        if (view) {
          view.classList.remove("has-workspace-rail", "has-workspace-under", "has-dense-bank", "has-narrow-workspace");
        }
      }
      const scrollbar = Math.max(0, bank.offsetHeight - bank.clientHeight);
      deck.style.setProperty("--bank-scrollbar", scrollbar + "px");
    }

    // VIEW 0: MIXER
    function renderMixerView(ws, st) {
      const prevWorkspace = ws.querySelector(".mixer-workspace");
      const prevBody = prevWorkspace ? prevWorkspace.querySelector(".work-body") : null;
      const prevScrollTop = prevBody ? prevBody.scrollTop : 0;
      const prevDeck = ws.querySelector(".mixer-deck");
      const prevBank = ws.querySelector(".mixer-bank");
      const useDeckScroller = window.innerWidth <= 900;
      const prevBankLeft = useDeckScroller
        ? (prevDeck ? prevDeck.scrollLeft : 0)
        : (prevBank ? prevBank.scrollLeft : 0);
      const prevSelectedId = ws.querySelector(".channel-strip.selected")?.dataset.stripId || null;

      let stripsHtml = "";
      if ((st.channels || []).length === 0) {
        stripsHtml += `<div class="mixer-empty">
          <span class="work-module-title">NO INPUT CHANNELS</span>
          <button type="button" class="hw-btn touch active" onclick="window.prismix.addChannel()">+ ADD INPUT CHANNEL</button>
        </div>`;
      } else {
        st.channels.forEach(ch => { stripsHtml += buildStripCardHtml(ch, false, false); });
      }
      (st.aux_buses || []).forEach(aux => { stripsHtml += buildStripCardHtml(aux, false, true); });

      const sel = getSelectedChannelObj() || st.master;
      const selectedId = sel ? String(sel.id) : null;
      const selectionChanged = selectedId !== prevSelectedId;
      const masterHtml = st.master ? buildStripCardHtml(st.master, true, false) : "";
      ws.innerHTML = `<div class="mixer-view">
        <div class="mixer-deck">
          <div class="mixer-bank" id="mixerScrollBay" role="group" aria-label="Input and bus channel bank">
            <div class="strip-rail">${stripsHtml}</div>
          </div>
          <div class="mixer-master-bay" aria-label="Master section">${masterHtml}</div>
        </div>
        ${sel ? buildSelectedWorkspaceHtml(st, sel) : ""}
      </div>`;

      // Rebuilding the bank must not pull the user back to channel 1 after every
      // state update. Bring a newly selected strip into view, otherwise keep the
      // current scroll position.
      const nextDeck = ws.querySelector(".mixer-deck");
      const nextBank = ws.querySelector(".mixer-bank");
      const nextSelected = ws.querySelector(".channel-strip.selected");
      const nextScroller = useDeckScroller ? nextDeck : nextBank;
      const alignStrip = () => {
        if (!nextScroller || !nextSelected) return;
        if (nextSelected.classList.contains("master-strip")) {
          nextScroller.scrollLeft = nextScroller.scrollWidth;
        } else {
          const selLeft = nextSelected.offsetLeft;
          const selWidth = nextSelected.offsetWidth;
          const scrollerWidth = nextScroller.clientWidth;
          if (scrollerWidth > 0 && selWidth > 0) {
            nextScroller.scrollLeft = Math.max(0, selLeft - (scrollerWidth - selWidth) / 2);
          } else {
            nextSelected.scrollIntoView({ block: "nearest", inline: "center" });
          }
        }
      };
      if (nextScroller) {
        nextScroller.scrollLeft = prevBankLeft;
        if (selectionChanged && nextSelected && nextScroller.contains(nextSelected)) {
          alignStrip();
          requestAnimationFrame(alignStrip);
        }
      }
      const nextBody = ws.querySelector(".work-body");
      if (nextBody) nextBody.scrollTop = selectionChanged ? 0 : prevScrollTop;

      syncMixerDeckMetrics();
    }


    // VIEW 1: CHANNEL STRIP EDITOR
    function renderChannelView(ws, st, ch) {
      const caps = ch.capabilities || {};
      const inputSources = (st.routing && st.routing.input_sources) || [];
      let srcOptions = `<option value="-1" ${ch.input_source_index < 0 ? "selected" : ""}>UNASSIGNED</option>`;
      inputSources.forEach((lbl, idx) => {
        srcOptions += `<option value="${idx}" ${ch.input_source_index === idx ? "selected" : ""}>${esc(lbl)}</option>`;
      });

      const preampHtml = caps.has_preamp ? `
        <div class="hw-panel">
          <div class="hw-panel-head">
            <span class="hw-panel-title">PREAMP &amp; INPUT STAGE</span>
            <span class="param-val">${esc(ch.input_label || "UNASSIGNED")}</span>
          </div>
          <div class="param-grid">
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">INPUT PATCH</span></div>
              <select class="hw-select" onchange="window.prismix.cmd({action:'set_input_source',channel:${ch.id},source:Number(this.value)})">${srcOptions}</select>
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">FORMAT</span></div>
              <div style="display:grid;grid-template-columns:1fr 1fr;gap:4px;">
                <button type="button" class="hw-btn ${ch.format === 0 ? "active" : ""}" onclick="window.prismix.cmd({action:'set_format',channel:${ch.id},value:0})">MONO</button>
                <button type="button" class="hw-btn ${ch.format === 1 ? "active" : ""}" onclick="window.prismix.cmd({action:'set_format',channel:${ch.id},value:1})">STEREO</button>
              </div>
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">PREAMP GAIN</span><span class="param-val" id="chGainVal">${formatGainDb(ch.gain)}</span></div>
              <input type="range" class="hw-range" min="0" max="1" step="0.005" value="${ch.gain}"
                     oninput="document.getElementById('chGainVal').textContent=window.prismix.fmtGain(this.value);window.prismix.coalesce('gain_${ch.id}',{action:'set_gain',channel:${ch.id},value:Number(this.value)})">
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">PREAMP SWITCHES</span></div>
              <div style="display:grid;grid-template-columns:1fr 1fr;gap:4px;">
                <button type="button" class="hw-btn ${ch.phantom_on ? "mute-btn on" : ""}" onclick="window.prismix.cmd({action:'set_phantom',channel:${ch.id},value:${!ch.phantom_on}})">+48V</button>
                <button type="button" class="hw-btn ${ch.polarity_inverted ? "solo-btn on" : ""}" onclick="window.prismix.cmd({action:'set_polarity',channel:${ch.id},value:${!ch.polarity_inverted}})">Ø PHASE</button>
              </div>
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">HIGH-PASS FILTER</span><button type="button" class="hw-btn ${ch.hpf_on ? "active" : ""}" style="min-height:24px;padding:0 8px;" onclick="window.prismix.cmd({action:'set_hpf_enabled',channel:${ch.id},value:${!ch.hpf_on}})">${ch.hpf_on ? "ON" : "BYP"}</button></div>
              <div class="param-head"><span class="param-lbl">CUTOFF</span><span class="param-val" id="chHpfVal">${Math.round(ch.hpf_frequency || 80)} Hz</span></div>
              <input type="range" class="hw-range" min="20" max="400" step="1" value="${ch.hpf_frequency || 80}"
                     oninput="document.getElementById('chHpfVal').textContent=Math.round(this.value)+' Hz';window.prismix.coalesce('hpf_${ch.id}',{action:'set_hpf_freq',channel:${ch.id},value:Number(this.value)})">
            </div>
          </div>
        </div>` : "";

      const sendsHtml = caps.has_aux_sends ? `
        <div class="hw-panel">
          <div class="hw-panel-head">
            <span class="hw-panel-title">AUX SENDS 1–4</span>
          </div>
          <div class="param-grid">
            ${[0, 1, 2, 3].map(a => {
              const lvl = ch.sends ? ch.sends[a] : 0;
              const en = ch.send_enabled ? ch.send_enabled[a] : true;
              const pre = ch.send_pre_fader ? ch.send_pre_fader[a] : false;
              return `<div class="param-cell">
                <div class="param-head">
                  <span class="param-lbl">AUX 0${a + 1}</span>
                  <span class="param-val" id="auxSendVal_${a}">${Math.round(lvl * 100)}%</span>
                </div>
                <input type="range" class="hw-range" min="0" max="1" step="0.01" value="${lvl}"
                       oninput="document.getElementById('auxSendVal_${a}').textContent=Math.round(this.value*100)+'%';window.prismix.coalesce('aux_${ch.id}_${a}',{action:'set_aux_send',channel:${ch.id},aux:${a},value:Number(this.value)})">
                <div style="display:grid;grid-template-columns:1fr 1fr;gap:4px;">
                  <button type="button" class="hw-btn ${en ? "active" : ""}" onclick="window.prismix.cmd({action:'set_aux_send_enabled',channel:${ch.id},aux:${a},value:${!en}})">${en ? "ON" : "MUTE"}</button>
                  <button type="button" class="hw-btn ${pre ? "solo-btn on" : ""}" onclick="window.prismix.cmd({action:'set_aux_send_pre',channel:${ch.id},aux:${a},value:${!pre}})">${pre ? "PRE" : "POST"}</button>
                </div>
              </div>`;
            }).join("")}
          </div>
        </div>` : "";

      const spkDelayHtml = caps.has_speaker_delay ? `
        <div class="hw-panel">
          <div class="hw-panel-head">
            <span class="hw-panel-title">MASTER SPEAKER ALIGNMENT DELAY</span>
            <div style="display:flex;gap:6px;">
              <button type="button" class="hw-btn ${ch.speaker_delay_link ? "active" : ""}" onclick="window.prismix.cmd({action:'set_speaker_delay',link:${!ch.speaker_delay_link}})">L/R LINK</button>
              <button type="button" class="hw-btn ${ch.speaker_delay_enabled ? "green-on" : ""}" onclick="window.prismix.cmd({action:'set_speaker_delay',enabled:${!ch.speaker_delay_enabled}})">${ch.speaker_delay_enabled ? "ACTIVE" : "BYPASSED"}</button>
            </div>
          </div>
          <div class="param-grid">
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">LEFT DELAY</span><span class="param-val" id="spkDelL">${(ch.speaker_delay_l_ms || 0).toFixed(1)} ms</span></div>
              <input type="range" class="hw-range" min="0" max="100" step="0.5" value="${ch.speaker_delay_l_ms || 0}"
                     oninput="document.getElementById('spkDelL').textContent=Number(this.value).toFixed(1)+' ms';window.prismix.coalesce('spkl',{action:'set_speaker_delay',delay_l:Number(this.value)})">
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">RIGHT DELAY</span><span class="param-val" id="spkDelR">${(ch.speaker_delay_r_ms || 0).toFixed(1)} ms</span></div>
              <input type="range" class="hw-range" min="0" max="100" step="0.5" value="${ch.speaker_delay_r_ms || 0}"
                     oninput="document.getElementById('spkDelR').textContent=Number(this.value).toFixed(1)+' ms';window.prismix.coalesce('spkr',{action:'set_speaker_delay',delay_r:Number(this.value)})">
            </div>
          </div>
        </div>` : "";

      ws.innerHTML = `<div class="screen-pad">
        <div class="screen-toolbar">
          <div class="toolbar-group">
            <strong style="font-size:14px;letter-spacing:0.4px;">${esc(ch.name)}</strong>
            <button type="button" class="hw-btn" onclick="window.prismix.promptRenameChannel(${ch.id})">RENAME</button>
            ${(!caps.is_master && !caps.is_aux) ? `<button type="button" class="hw-btn danger" onclick="window.prismix.deleteChannel(${ch.id})">DELETE CH</button>` : ""}
          </div>
          <div class="toolbar-group">
            <button type="button" class="hw-btn mute-btn ${ch.mute ? "on" : ""}" onclick="window.prismix.toggleMute(${ch.id}, ${!ch.mute})">MUTE</button>
            ${!caps.is_master ? `<button type="button" class="hw-btn solo-btn ${ch.solo ? "on" : ""}" onclick="window.prismix.toggleSolo(${ch.id}, ${!ch.solo})">SOLO</button>` : ""}
            <button type="button" class="hw-btn" onclick="window.prismix.setView(2)">OPEN EQ</button>
            <button type="button" class="hw-btn" onclick="window.prismix.setView(3)">OPEN DYNAMICS</button>
            ${caps.has_fx_rack ? `<button type="button" class="hw-btn" onclick="window.prismix.setView(4)">OPEN FX RACK</button>` : ""}
          </div>
        </div>
        <div class="panel-grid">
          ${preampHtml}
          ${sendsHtml}
          ${spkDelayHtml}
          <div class="hw-panel">
            <div class="hw-panel-head">
              <span class="hw-panel-title">OUTPUT FADER &amp; PAN</span>
              <span class="param-val" id="chFaderOutVal">${formatFaderDb(ch.fader)}</span>
            </div>
            <div class="param-grid">
              <div class="param-cell">
                <div class="param-head"><span class="param-lbl">FADER LEVEL</span><span class="param-val" id="chFaderCellVal">${formatFaderDb(ch.fader)}</span></div>
                <input type="range" class="hw-range" min="0" max="1" step="0.005" value="${ch.fader}"
                       oninput="document.getElementById('chFaderCellVal').textContent=window.prismix.fmtFader(this.value);document.getElementById('chFaderOutVal').textContent=window.prismix.fmtFader(this.value);window.prismix.coalesce('fader_${ch.id}',{action:'set_fader',channel:${ch.id},value:Number(this.value)})">
              </div>
              <div class="param-cell">
                <div class="param-head"><span class="param-lbl">STEREO PAN</span><span class="param-val" id="chPanCellVal">${formatPan(ch.pan)}</span></div>
                <input type="range" class="hw-range" min="0" max="1" step="0.01" value="${ch.pan}"
                       oninput="document.getElementById('chPanCellVal').textContent=window.prismix.fmtPan(this.value);window.prismix.coalesce('pan_${ch.id}',{action:'set_pan',channel:${ch.id},value:Number(this.value)})">
              </div>
              ${!caps.is_master ? `<div class="param-cell">
                <div class="param-head"><span class="param-lbl">MAIN L/R BUS</span></div>
                <button type="button" class="hw-btn ${ch.route_to_main ? "green-on" : ""}" onclick="window.prismix.cmd({action:'set_output_route',channel:${ch.id},route_to_main:${!ch.route_to_main}})">
                  ${ch.route_to_main ? "ASSIGNED TO MAIN L/R" : "UNASSIGNED"}
                </button>
              </div>` : ""}
            </div>
          </div>
        </div>
      </div>`;
    }

    // VIEW 2: PARAMETRIC & GRAPHIC EQ
    function renderEqView(ws, st, ch) {
      const isGeq = Boolean(ch.geq_mode_active);
      const b = store.activeEqBand;
      const bFreq = (ch.eq_freq && ch.eq_freq[b]) || 1000;
      const bGain = (ch.eq_gain && ch.eq_gain[b]) || 0;
      const bQ = (ch.eq_q && ch.eq_q[b]) || 1.0;
      const bEn = (ch.eq_enabled && ch.eq_enabled[b]) ?? true;
      const bType = (ch.eq_filter_type && ch.eq_filter_type[b]) || 1;

      let geqFadersHtml = "";
      if (isGeq) {
        const gains = ch.geq_gains || new Array(31).fill(0);
        geqFadersHtml = `<div class="geq-faders-bank">
          ${ISO_FREQS_31.map((f, idx) => {
            const g = gains[idx] || 0;
            const lbl = f >= 1000 ? (f / 1000) + "k" : f;
            return `<div class="geq-col">
              <span class="param-val" style="font-size:10px;">${(g >= 0 ? "+" : "") + g.toFixed(1)}</span>
              <div class="geq-v-track">
                <input type="range" class="geq-v-slider" min="-12" max="12" step="0.5" value="${g}"
                       aria-label="${lbl} hertz graphic EQ band" aria-valuetext="${(g >= 0 ? "+" : "") + g.toFixed(1)} decibels"
                       oninput="window.prismix.coalesce('geq_${ch.id}_${idx}',{action:'set_geq_band',channel:${ch.id},band:${idx},value:Number(this.value)})">
              </div>
              <span class="param-lbl" style="font-size:9.5px;">${lbl}</span>
            </div>`;
          }).join("")}
        </div>`;
      }

      ws.innerHTML = `<div class="screen-pad">
        <div class="screen-toolbar">
          <div class="toolbar-group">
            <strong style="font-size:14px;">EQ // ${esc(ch.name)}</strong>
            <button type="button" class="hw-btn ${isGeq ? "" : "active"}" onclick="window.prismix.cmd({action:'set_geq_mode',channel:${ch.id},value:false})">4-BAND PEQ</button>
            <button type="button" class="hw-btn ${isGeq ? "active" : ""}" onclick="window.prismix.cmd({action:'set_geq_mode',channel:${ch.id},value:true})">31-BAND GEQ</button>
            <button type="button" class="hw-btn ${ch.eq_bypass ? "danger" : "green-on"}" onclick="window.prismix.cmd({action:'set_eq_bypass',channel:${ch.id},value:${!ch.eq_bypass}})">${ch.eq_bypass ? "BYPASS" : "ACTIVE"}</button>
          </div>
          <div class="toolbar-group">
            <button type="button" class="hw-btn" onclick="window.prismix.cmd({action:'set_rta_config',mode:0})">RTA 31</button>
            <button type="button" class="hw-btn" onclick="window.prismix.cmd({action:'set_rta_config',mode:1})">RTA 63</button>
            <button type="button" class="hw-btn active" onclick="window.prismix.cmd({action:'set_rta_config',mode:2})">RTA FFT</button>
            <button type="button" class="hw-btn" onclick="window.prismix.cmd({action:isGeq?'flatten_geq':'flatten_peq',channel:${ch.id}})">FLATTEN</button>
          </div>
        </div>

        <div class="curve-canvas-wrap">
          <canvas class="eq-curve-canvas" id="eqRtaCanvas"></canvas>
        </div>

        ${isGeq ? geqFadersHtml : `
        <div class="hw-panel">
          <div class="hw-panel-head">
            <div style="display:flex;gap:6px;">
              ${[0, 1, 2, 3].map(i => `<button type="button" class="hw-btn ${i === b ? "active" : ""}" onclick="window.prismix.selectEqBand(${i})">BAND 0${i + 1}</button>`).join("")}
            </div>
            <button type="button" class="hw-btn ${bEn ? "green-on" : ""}" onclick="window.prismix.cmd({action:'set_peq_band',channel:${ch.id},band:${b},enabled:${!bEn}})">${bEn ? "BAND ON" : "BYPASS"}</button>
          </div>
          <div class="param-grid">
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">FILTER SHAPE</span></div>
              <select class="hw-select" onchange="window.prismix.cmd({action:'set_peq_band',channel:${ch.id},band:${b},filter_type:Number(this.value)})">
                <option value="0" ${bType === 0 ? "selected" : ""}>LOW SHELF</option>
                <option value="1" ${bType === 1 ? "selected" : ""}>PARAMETRIC BELL</option>
                <option value="2" ${bType === 2 ? "selected" : ""}>HIGH SHELF</option>
                <option value="3" ${bType === 3 ? "selected" : ""}>HIGH-PASS (HPF)</option>
                <option value="4" ${bType === 4 ? "selected" : ""}>LOW-PASS (LPF)</option>
              </select>
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">FREQUENCY</span><span class="param-val" data-eq-freq-lbl="${b}">${formatHz(bFreq)}</span></div>
              <input type="range" class="hw-range" min="0" max="1" step="0.002" value="${freqToNorm(bFreq)}"
                     data-eq-range="freq_${b}" aria-label="Band ${b + 1} frequency" aria-valuetext="${formatHz(bFreq)}"
                     oninput="window.prismix.onPeqSlider(${ch.id}, ${b}, 'freq', this.value)">
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">GAIN</span><span class="param-val" data-eq-gain-lbl="${b}">${(bGain >= 0 ? "+" : "") + bGain.toFixed(1)} dB</span></div>
              <input type="range" class="hw-range" min="-18" max="18" step="0.2" value="${bGain}"
                     data-eq-range="gain_${b}" aria-label="Band ${b + 1} gain" aria-valuetext="${(bGain >= 0 ? "+" : "") + bGain.toFixed(1)} decibels"
                     oninput="window.prismix.onPeqSlider(${ch.id}, ${b}, 'gain', this.value)">
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">Q FACTOR</span><span class="param-val" data-eq-q-lbl="${b}">${bQ.toFixed(2)}</span></div>
              <input type="range" class="hw-range" min="0.2" max="10" step="0.05" value="${bQ}"
                     data-eq-range="q_${b}" aria-label="Band ${b + 1} Q factor" aria-valuetext="${bQ.toFixed(2)}"
                     oninput="window.prismix.onPeqSlider(${ch.id}, ${b}, 'q', this.value)">
            </div>
          </div>
        </div>`}
      </div>`;
    }

    // VIEW 3: DYNAMICS (Gate, Compressor, Limiter)
    function renderDynamicsView(ws, st, ch) {
      const caps = ch.capabilities || {};
      const gateHtml = caps.has_gate ? `
        <div class="hw-panel">
          <div class="hw-panel-head">
            <span class="hw-panel-title">NOISE GATE</span>
            <button type="button" class="hw-btn ${ch.gate_on ? "green-on" : ""}" onclick="window.prismix.cmd({action:'set_gate_enabled',channel:${ch.id},value:${!ch.gate_on}})">${ch.gate_on ? "GATE ACTIVE" : "BYPASSED"}</button>
          </div>
          ${buildDynamicsGrDisplay(ch, "gate", "GATE")}
          <div class="curve-canvas-wrap">
            <canvas class="dyn-curve-canvas" id="gateCurveCanvas"></canvas>
          </div>
          <div class="param-grid">
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">THRESHOLD</span><span class="param-val" id="gThreshVal">${(ch.gate_thresh || -40).toFixed(1)} dB</span></div>
              <input type="range" class="hw-range" min="-80" max="0" step="0.5" value="${ch.gate_thresh || -40}"
                     oninput="document.getElementById('gThreshVal').textContent=Number(this.value).toFixed(1)+' dB';window.prismix.onGateParam(${ch.id},'thresh',this.value)">
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">RANGE / REDUCTION</span><span class="param-val" id="gRangeVal">${(ch.gate_range || -40).toFixed(1)} dB</span></div>
              <input type="range" class="hw-range" min="-80" max="0" step="0.5" value="${ch.gate_range || -40}"
                     oninput="document.getElementById('gRangeVal').textContent=Number(this.value).toFixed(1)+' dB';window.prismix.onGateParam(${ch.id},'range',this.value)">
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">ATTACK</span><span class="param-val">${(ch.gate_attack || 1.5).toFixed(1)} ms</span></div>
              <input type="range" class="hw-range" min="0.1" max="50" step="0.5" value="${ch.gate_attack || 1.5}"
                     oninput="window.prismix.onGateParam(${ch.id},'attack',this.value)">
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">RELEASE</span><span class="param-val">${(ch.gate_release || 150).toFixed(0)} ms</span></div>
              <input type="range" class="hw-range" min="10" max="1000" step="5" value="${ch.gate_release || 150}"
                     oninput="window.prismix.onGateParam(${ch.id},'release',this.value)">
            </div>
          </div>
        </div>` : "";

      const compHtml = caps.has_comp ? `
        <div class="hw-panel">
          <div class="hw-panel-head">
            <span class="hw-panel-title">VCA COMPRESSOR</span>
            <button type="button" class="hw-btn ${ch.comp_bypass ? "danger" : "green-on"}" onclick="window.prismix.cmd({action:'set_comp_bypass',channel:${ch.id},value:${!ch.comp_bypass}})">${ch.comp_bypass ? "BYPASSED" : "ACTIVE"}</button>
          </div>
          ${buildDynamicsGrDisplay(ch, "comp", "COMP")}
          <div class="curve-canvas-wrap">
            <canvas class="dyn-curve-canvas" id="compCurveCanvas"></canvas>
          </div>
          <div class="param-grid">
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">THRESHOLD</span><span class="param-val" id="cThreshVal">${(ch.comp_thresh || -18).toFixed(1)} dB</span></div>
              <input type="range" class="hw-range" min="-60" max="0" step="0.5" value="${ch.comp_thresh || -18}"
                     oninput="document.getElementById('cThreshVal').textContent=Number(this.value).toFixed(1)+' dB';window.prismix.onCompParam(${ch.id},'thresh',this.value)">
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">RATIO</span><span class="param-val" id="cRatioVal">${(ch.comp_ratio || 4.0).toFixed(1)}:1</span></div>
              <input type="range" class="hw-range" min="1" max="20" step="0.2" value="${ch.comp_ratio || 4.0}"
                     oninput="document.getElementById('cRatioVal').textContent=Number(this.value).toFixed(1)+':1';window.prismix.onCompParam(${ch.id},'ratio',this.value)">
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">ATTACK</span><span class="param-val">${(ch.comp_attack || 10).toFixed(1)} ms</span></div>
              <input type="range" class="hw-range" min="0.1" max="100" step="0.5" value="${ch.comp_attack || 10}"
                     oninput="window.prismix.onCompParam(${ch.id},'attack',this.value)">
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">RELEASE</span><span class="param-val">${(ch.comp_release || 100).toFixed(0)} ms</span></div>
              <input type="range" class="hw-range" min="10" max="1500" step="10" value="${ch.comp_release || 100}"
                     oninput="window.prismix.onCompParam(${ch.id},'release',this.value)">
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">KNEE</span><span class="param-val">${(ch.comp_knee || 6).toFixed(1)} dB</span></div>
              <input type="range" class="hw-range" min="0" max="18" step="0.5" value="${ch.comp_knee || 6}"
                     oninput="window.prismix.onCompParam(${ch.id},'knee',this.value)">
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">MAKEUP GAIN</span><span class="param-val">${(ch.comp_makeup || 0).toFixed(1)} dB</span></div>
              <input type="range" class="hw-range" min="-12" max="24" step="0.5" value="${ch.comp_makeup || 0}"
                     oninput="window.prismix.onCompParam(${ch.id},'makeup',this.value)">
            </div>
          </div>
        </div>` : "";

      const limHtml = caps.has_limiter ? `
        <div class="hw-panel">
          <div class="hw-panel-head">
            <span class="hw-panel-title">BRICKWALL PEAK LIMITER</span>
            <button type="button" class="hw-btn ${ch.limiter_enabled ? "green-on" : ""}" onclick="window.prismix.cmd({action:'set_limiter_enabled',channel:${ch.id},value:${!ch.limiter_enabled}})">${ch.limiter_enabled ? "LIMITER ON" : "BYPASSED"}</button>
          </div>
          ${buildDynamicsGrDisplay(ch, "lim", "LIMITER")}
          <div class="param-grid">
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">CEILING / THRESHOLD</span><span class="param-val" id="limCeilVal">${(ch.limiter_ceiling_db || -0.5).toFixed(1)} dBFS</span></div>
              <input type="range" class="hw-range" min="-18" max="0" step="0.1" value="${ch.limiter_ceiling_db || -0.5}"
                     oninput="document.getElementById('limCeilVal').textContent=Number(this.value).toFixed(1)+' dBFS';window.prismix.onLimParam(${ch.id},'ceiling',this.value)">
            </div>
            <div class="param-cell">
              <div class="param-head"><span class="param-lbl">RELEASE TIME</span><span class="param-val">${(ch.limiter_release_ms || 100).toFixed(0)} ms</span></div>
              <input type="range" class="hw-range" min="10" max="1000" step="10" value="${ch.limiter_release_ms || 100}"
                     oninput="window.prismix.onLimParam(${ch.id},'release',this.value)">
            </div>
          </div>
        </div>` : "";

      ws.innerHTML = `<div class="screen-pad">
        <div class="screen-toolbar">
          <strong style="font-size:14px;">DYNAMICS PROCESSORS // ${esc(ch.name)}</strong>
        </div>
        <div class="panel-grid">
          ${gateHtml}
          ${compHtml}
          ${limHtml}
        </div>
      </div>`;
    }

    // VIEW 4: INSERT FX RACK
    function renderFxView(ws, st, ch) {
      const caps = ch.capabilities || {};
      if (!caps.has_fx_rack) {
        ws.innerHTML = `<div class="screen-pad">
          <div class="hw-panel" style="align-items:center;justify-content:center;min-height:300px;text-align:center;">
            <span class="hw-panel-title">INSERT FX RACK UNAVAILABLE</span>
            <p style="color:var(--text-secondary);max-width:420px;font-size:12px;line-height:1.5;">Master bus uses dedicated high-resolution brickwall peak limiting and alignment delay. Insert FX rack is available on Input channels and AUX buses.</p>
          </div>
        </div>`;
        return;
      }

      const rack = ch.fx_rack || [];
      const slotsHtml = [0, 1, 2, 3, 4, 5, 6, 7].map(slotIdx => {
        const item = rack[slotIdx] || null;
        const isAct = (slotIdx === store.activeFxSlot);
        return `<div class="fx-slot-card ${isAct ? "active" : ""} ${item && item.bypassed ? "bypassed" : ""}" onclick="window.prismix.selectFxSlot(${slotIdx})">
          <div style="display:flex;justify-content:space-between;align-items:center;">
            <span class="param-lbl">SLOT 0${slotIdx + 1}</span>
            <span class="param-val" style="font-size:10px;">${item ? esc(item.category || "PLUGIN") : "EMPTY"}</span>
          </div>
          <div style="font-size:12px;font-weight:700;color:var(--text-primary);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;">
            ${item ? esc(item.name) : "+ ASSIGN"}
          </div>
        </div>`;
      }).join("");

      const activeInst = rack[store.activeFxSlot] || null;
      let editorHtml = "";
      if (activeInst) {
        const params = activeInst.parameters || [];
        editorHtml = `<div class="hw-panel">
          <div class="hw-panel-head">
            <span class="hw-panel-title">${esc(activeInst.name)} (SLOT 0${store.activeFxSlot + 1})</span>
            <div style="display:flex;gap:6px;">
              <button type="button" class="hw-btn ${activeInst.bypassed ? "danger" : "green-on"}"
                      onclick="window.prismix.cmd({action:'set_slot_bypass',channel:${ch.id},slot:${store.activeFxSlot},value:${!activeInst.bypassed}})">${activeInst.bypassed ? "BYPASS" : "ACTIVE"}</button>
              <button type="button" class="hw-btn danger"
                      onclick="window.prismix.cmd({action:'remove_plugin',channel:${ch.id},slot:${store.activeFxSlot}})">REMOVE</button>
            </div>
          </div>
          <div class="param-grid">
            ${params.map(p => {
              if (p.is_toggle) {
                const isOn = p.value >= 0.5;
                return `<div class="param-cell">
                  <div class="param-head"><span class="param-lbl">${esc(p.name)}</span><span class="param-val">${isOn ? "ON" : "OFF"}</span></div>
                  <button type="button" class="hw-btn ${isOn ? "active" : ""}"
                          onclick="window.prismix.cmd({action:'set_plugin_param',channel:${ch.id},slot:${store.activeFxSlot},param_id:'${esc(p.id)}',value:${isOn ? 0 : 1}})">
                    ${isOn ? "ENABLED" : "DISABLED"}
                  </button>
                </div>`;
              }
              const step = p.step > 0 ? p.step : ((p.max - p.min) / 200);
              return `<div class="param-cell">
                <div class="param-head">
                  <span class="param-lbl" title="${esc(p.name)}">${esc(p.name)}</span>
                  <span class="param-val" id="fxP_${esc(p.id)}">${Number(p.value).toFixed(step >= 1 ? 0 : 2)} ${esc(p.unit || "")}</span>
                </div>
                <input type="range" class="hw-range" min="${p.min}" max="${p.max}" step="${step}" value="${p.value}"
                       oninput="document.getElementById('fxP_${esc(p.id)}').textContent=Number(this.value).toFixed(${step >= 1 ? 0 : 2})+' ${esc(p.unit || "")}';window.prismix.coalesce('fx_${ch.id}_${store.activeFxSlot}_${esc(p.id)}',{action:'set_plugin_param',channel:${ch.id},slot:${store.activeFxSlot},param_id:'${esc(p.id)}',value:Number(this.value)})">
              </div>`;
            }).join("")}
          </div>
        </div>`;
      } else {
        editorHtml = `<div class="hw-panel" style="align-items:center;justify-content:center;min-height:180px;">
          <span class="hw-panel-title">NO INSERT EFFECT IN SELECTED SLOT</span>
          <button type="button" class="hw-btn touch active" onclick="window.prismix.openFxModal()">+ BROWSE BUILT-IN / LV2 / NAM / PATCHSTORAGE</button>
        </div>`;
      }

      ws.innerHTML = `<div class="screen-pad">
        <div class="screen-toolbar">
          <div class="toolbar-group">
            <strong style="font-size:14px;">INSERT FX RACK // ${esc(ch.name)}</strong>
            <button type="button" class="hw-btn active" onclick="window.prismix.openFxModal()">+ ADD PLUGIN</button>
          </div>
          <div class="toolbar-group">
            <button type="button" class="hw-btn" onclick="window.prismix.promptSavePreset(${ch.id})">SAVE PRESET</button>
          </div>
        </div>
        <div class="fx-slots-strip">${slotsHtml}</div>
        ${editorHtml}
      </div>`;
    }

    // VIEW 5: ROUTING MATRIX
    function renderRoutingView(ws, st, ch) {
      const rt = st.routing || {};
      const inputs = rt.input_sources || [];
      const buses = rt.internal_buses || [];
      const outputs = rt.output_destinations || [];
      const chBusMatrix = rt.channel_bus_matrix || [];
      const busMainLr = rt.bus_main_lr || [];
      const outPatches = rt.output_patches || [];

      const patchSourceNames = [
        ...buses.map((b, i) => ({ id: i, name: b })),
        { id: 8, name: "MAIN L" }, { id: 9, name: "MAIN R" },
        { id: 10, name: "AUX 01" }, { id: 11, name: "AUX 02" }, { id: 12, name: "AUX 03" }, { id: 13, name: "AUX 04" }
      ];
      const auxMastersHtml = (st.aux_buses || []).length ? `<div class="hw-panel">
        <div class="hw-panel-head"><span class="hw-panel-title">AUXILIARY BUS MASTERS</span><span class="param-lbl">SELECT A BUS TO EDIT ITS EQ, DYNAMICS, OR FX</span></div>
        <div class="aux-master-grid">
          ${(st.aux_buses || []).map((aux, idx) => {
            const selected = Number(st.selected_aux_bus) === idx;
            const faderKey = `aux_fader_${aux.id}`;
            const returnKey = `aux_return_${aux.id}`;
            return `<section class="aux-master-card ${selected ? "selected" : ""}" aria-label="${esc(aux.name)} master">
              <div class="aux-master-head">
                <strong>${esc(aux.name)}</strong>
                <span class="param-lbl">STEREO RETURN</span>
              </div>
              <div style="display:flex;align-items:center;gap:6px;">
                <canvas class="aux-master-meter" data-meter-canvas="${aux.id}" aria-label="${esc(aux.name)} live meter"></canvas>
                <span class="clip-led ${aux.clip ? "hot" : ""}" data-clip-led="${aux.id}">CLIP</span>
              </div>
              <div class="aux-master-control">
                <span class="param-lbl">FADER</span>
                <input type="range" class="hw-range" min="0" max="1" step="0.005" value="${aux.fader}"
                       aria-label="${esc(aux.name)} fader"
                       oninput="document.getElementById('auxFaderVal_${aux.id}').textContent=window.prismix.fmtFader(this.value);window.prismix.coalesce('${faderKey}',{action:'set_fader',channel:${aux.id},value:Number(this.value)})"
                       onpointerdown="window.prismix.beginWorkControl('${faderKey}')"
                       onpointerup="window.prismix.endWorkControl('${faderKey}')"
                       onchange="window.prismix.endWorkControl('${faderKey}')">
                <span class="param-val" id="auxFaderVal_${aux.id}">${formatFaderDb(aux.fader)}</span>
              </div>
              <div class="aux-master-control">
                <span class="param-lbl">RETURN</span>
                <input type="range" class="hw-range" min="0" max="1" step="0.005" value="${aux.return_gain}"
                       aria-label="${esc(aux.name)} return gain"
                       oninput="document.getElementById('auxReturnVal_${aux.id}').textContent=window.prismix.fmtFader(this.value);window.prismix.coalesce('${returnKey}',{action:'set_return_gain',channel:${aux.id},value:Number(this.value)})"
                       onpointerdown="window.prismix.beginWorkControl('${returnKey}')"
                       onpointerup="window.prismix.endWorkControl('${returnKey}')"
                       onchange="window.prismix.endWorkControl('${returnKey}')">
                <span class="param-val" id="auxReturnVal_${aux.id}">${formatFaderDb(aux.return_gain)}</span>
              </div>
              <div class="aux-master-actions">
                <button type="button" class="hw-btn mute-btn ${aux.mute ? "on" : ""}"
                        aria-pressed="${Boolean(aux.mute)}" onclick="window.prismix.toggleMute(${aux.id},${!aux.mute})">MUTE</button>
                <button type="button" class="hw-btn ${aux.route_to_main ? "green-on" : ""}"
                        aria-pressed="${Boolean(aux.route_to_main)}"
                        onclick="window.prismix.cmd({action:'set_output_route',channel:${aux.id},route_to_main:${!aux.route_to_main}})">${aux.route_to_main ? "TO MAIN" : "MAIN OFF"}</button>
                <button type="button" class="hw-btn ${selected ? "active" : ""}"
                        aria-pressed="${selected}" onclick="window.prismix.selectChannel(${aux.id})">${selected ? "ACTIVE TARGET" : "SELECT FOR EQ/DYN/FX"}</button>
              </div>
            </section>`;
          }).join("")}
        </div>
      </div>` : "";

      let contentHtml = "";
      if (store.routingSubMode === 0) {
        const stages = [
          { num: "1. INPUT PATCH", val: ch.input_label || "UNASSIGNED" },
          { num: "2. PREAMP / HPF", val: (ch.hpf_on ? ("HPF " + Math.round(ch.hpf_frequency || 80) + "Hz") : "FLAT") },
          { num: "3. INSERT FX", val: (ch.fx_rack ? ch.fx_rack.length : 0) + " ACTIVE" },
          { num: "4. EQ STAGE", val: ch.eq_bypass ? "BYPASSED" : (ch.geq_mode_active ? "31-BAND GEQ" : "4-BAND PEQ") },
          { num: "5. DYNAMICS", val: (!ch.comp_bypass ? "COMP ON" : "COMP BYP") + (ch.limiter_enabled ? " + LIM" : "") },
          { num: "6. FADER / PAN", val: formatFaderDb(ch.fader) + " · " + formatPan(ch.pan) },
          { num: "7. BUS ASSIGN", val: ch.route_to_main ? "MAIN L/R" : "UNASSIGNED" }
        ];
        contentHtml = `<div class="hw-panel">
          <div class="hw-panel-head"><span class="hw-panel-title">SIGNAL TOPOLOGY // ${esc(ch.name)}</span></div>
          <div class="param-grid">
            ${stages.map(s => `<div class="param-cell">
              <span class="param-lbl">${esc(s.num)}</span>
              <span class="param-val" style="color:var(--accent-cyan);">${esc(s.val)}</span>
            </div>`).join("")}
          </div>
        </div>
        <div class="hw-panel">
          <div class="hw-panel-head"><span class="hw-panel-title">PHYSICAL OUTPUT PATCHBAY (OUT 1–8)</span></div>
          <div class="param-grid">
            ${outputs.map((outName, oIdx) => {
              const cur = outPatches[oIdx] ?? 0;
              return `<div class="param-cell">
                <div class="param-head"><span class="param-lbl">${esc(outName)}</span></div>
                <select class="hw-select" onchange="window.prismix.cmd({action:'set_output_patch',output_port:${oIdx},source_id:Number(this.value)})">
                  ${patchSourceNames.map(ps => `<option value="${ps.id}" ${cur === ps.id ? "selected" : ""}>${esc(ps.name)}</option>`).join("")}
                </select>
              </div>`;
            }).join("")}
          </div>
        </div>
        ${auxMastersHtml}`;
      } else {
        contentHtml = `<div class="hw-panel">
          <div class="hw-panel-head"><span class="hw-panel-title">CHANNEL CROSSPOINT MATRIX</span></div>
          <div class="matrix-scroll">
            <table class="routing-table">
              <thead>
                <tr>
                  <th>CHANNEL</th>
                  <th>INPUT SOURCE</th>
                  <th>MAIN L/R</th>
                  ${buses.map(b => `<th>${esc(b)}</th>`).join("")}
                </tr>
              </thead>
              <tbody>
                ${(st.channels || []).map(c => {
                  const row = chBusMatrix[c.id - 1] || [];
                  return `<tr>
                    <td style="font-weight:700;text-align:left;">CH ${String(c.id).padStart(2, "0")} · ${esc(c.name)}</td>
                    <td>
                      <select class="hw-select" style="min-height:28px;" onchange="window.prismix.cmd({action:'set_input_source',channel:${c.id},source:Number(this.value)})">
                        <option value="-1" ${c.input_source_index < 0 ? "selected" : ""}>UNASSIGNED</option>
                        ${inputs.map((inp, idx) => `<option value="${idx}" ${c.input_source_index === idx ? "selected" : ""}>${esc(inp)}</option>`).join("")}
                      </select>
                    </td>
                    <td>
                      <button type="button" class="xpoint-btn ${c.route_to_main ? "patched" : ""}"
                              onclick="window.prismix.cmd({action:'set_output_route',channel:${c.id},route_to_main:${!c.route_to_main}})">L/R</button>
                    </td>
                    ${buses.map((_, bIdx) => {
                      const on = Boolean(row[bIdx]);
                      return `<td>
                        <button type="button" class="xpoint-btn ${on ? "patched" : ""}"
                                onclick="window.prismix.cmd({action:'set_channel_bus',channel:${c.id},bus:${bIdx},value:${!on}})">B${bIdx + 1}</button>
                      </td>`;
                    }).join("")}
                  </tr>`;
                }).join("")}
              </tbody>
            </table>
          </div>
        </div>`;
      }

      ws.innerHTML = `<div class="screen-pad">
        <div class="screen-toolbar">
          <div class="toolbar-group">
            <button type="button" class="hw-btn ${store.routingSubMode === 0 ? "active" : ""}" onclick="window.prismix.setRoutingSubMode(0)">SIGNAL TOPOLOGY</button>
            <button type="button" class="hw-btn ${store.routingSubMode === 1 ? "active" : ""}" onclick="window.prismix.setRoutingSubMode(1)">CROSSPOINT MATRIX</button>
          </div>
        </div>
        ${contentHtml}
      </div>`;
    }

    // VIEW 6: SYSTEM & THEME
    function systemBadge(text, tone = "") {
      return `<span class="system-badge ${tone}">${esc(text)}</span>`;
    }

    function signalBars(percent) {
      const p = Number(percent);
      const active = p < 0 ? 0 : Math.max(1, Math.min(4, Math.ceil(p / 25)));
      return `<span class="system-signal" aria-hidden="true">${[0, 1, 2, 3].map(i =>
        `<i class="${i < active ? "on" : ""}"></i>`).join("")}</span>`;
    }

    function drawQrToCanvas(canvasId, value) {
      const canvas = document.getElementById(canvasId);
      if (!canvas || !value) return false;
      try {
        const qr = qrcode(0, "M");
        qr.addData(value, "Byte");
        qr.make();
        const count = qr.getModuleCount();
        const scale = 5;
        const quiet = 4;
        const pixelSize = (count + quiet * 2) * scale;
        canvas.width = pixelSize;
        canvas.height = pixelSize;
        const ctx = canvas.getContext("2d");
        if (!ctx) return false;
        ctx.fillStyle = "#FFFFFF";
        ctx.fillRect(0, 0, pixelSize, pixelSize);
        ctx.fillStyle = "#0D0E0C";
        for (let row = 0; row < count; row += 1) {
          for (let col = 0; col < count; col += 1) {
            if (qr.isDark(row, col)) {
              ctx.fillRect((col + quiet) * scale, (row + quiet) * scale, scale, scale);
            }
          }
        }
        return true;
      } catch (_) {
        return false;
      }
    }

    function drawSystemQr() {
      const url = store.systemStatus && store.systemStatus.web_access
        ? store.systemStatus.web_access.url : "";
      if (!url) return;
      const canvas = document.getElementById("webAccessQrCanvas");
      if (!canvas) return;
      if (!drawQrToCanvas("webAccessQrCanvas", url)) {
        const frame = document.getElementById("webAccessQrFrame");
        if (frame) {
          frame.innerHTML = `<div class="system-qr-fallback">QR UNAVAILABLE</div>`;
        }
      }
    }

    async function copyTextToClipboard(text) {
      if (!text) {
        showToast("No Web UI address is available yet.", "err");
        return false;
      }
      try {
        if (navigator.clipboard && window.isSecureContext) {
          await navigator.clipboard.writeText(text);
        } else {
          const area = document.createElement("textarea");
          area.value = text;
          area.setAttribute("readonly", "");
          area.style.position = "fixed";
          area.style.opacity = "0";
          area.style.left = "-9999px";
          document.body.appendChild(area);
          area.select();
          const copied = document.execCommand("copy");
          document.body.removeChild(area);
          if (!copied) throw new Error("copy failed");
        }
        showToast("Web UI address copied.", "ok");
        return true;
      } catch (_) {
        showToast("Clipboard access was denied. Select the address to copy it manually.", "err");
        return false;
      }
    }

    function renderSystemView(ws, st) {
      const sys = st.system || {};
      const theme = (sys.theme) || { active_preset: 0, preset_name: "Graphite", colors: {} };
      const themePresets = ["GRAPHITE", "LEXICON-INSPIRED", "STUDIO BLUE", "MONOCHROME", "CUSTOM"];

      let contentHtml = "";
      if (store.systemSubMode === 0) {
        contentHtml = `<div class="panel-grid">
          <div class="hw-panel">
            <div class="hw-panel-head"><span class="hw-panel-title">AUDIO ENGINE MODE</span></div>
            <div style="display:grid;grid-template-columns:1fr 1fr;gap:6px;">
              <button type="button" class="hw-btn ${sys.audio_mode === 0 ? "active" : ""}" onclick="window.prismix.cmd({action:'set_audio_mode',mode:0})">SIMULATION</button>
              <button type="button" class="hw-btn ${sys.audio_mode === 1 ? "active" : ""}" onclick="window.prismix.cmd({action:'set_audio_mode',mode:1})">REAL AUDIO</button>
            </div>
            <div class="param-grid" style="margin-top:6px;">
              <div class="param-cell">
                <div class="param-head"><span class="param-lbl">SIGNAL GENERATOR</span></div>
                <select class="hw-select" onchange="window.prismix.cmd({action:'set_generator_signal',signal:Number(this.value)})">
                  <option value="0" ${sys.generator_signal === 0 ? "selected" : ""}>SINE 1 kHz</option>
                  <option value="1" ${sys.generator_signal === 1 ? "selected" : ""}>PINK NOISE</option>
                  <option value="2" ${sys.generator_signal === 2 ? "selected" : ""}>VOCAL SIM</option>
                  <option value="3" ${sys.generator_signal === 3 ? "selected" : ""}>FULL BAND</option>
                  <option value="4" ${sys.generator_signal === 4 ? "selected" : ""}>SWEEP 20-20k</option>
                </select>
              </div>
              <div class="param-cell">
                <div class="param-head"><span class="param-lbl">SAMPLE RATE</span></div>
                <select class="hw-select" onchange="window.prismix.cmd({action:'set_audio_config',sample_rate:Number(this.value)})">
                  <option value="44100" ${sys.actual_sample_rate === 44100 ? "selected" : ""}>44.1 kHz</option>
                  <option value="48000" ${sys.actual_sample_rate === 48000 ? "selected" : ""}>48.0 kHz</option>
                  <option value="96000" ${sys.actual_sample_rate === 96000 ? "selected" : ""}>96.0 kHz</option>
                </select>
              </div>
            </div>
          </div>
          <div class="hw-panel">
            <div class="hw-panel-head"><span class="hw-panel-title">PERSISTENCE &amp; SCENES</span></div>
            <div style="display:flex;gap:8px;">
              <button type="button" class="hw-btn touch" onclick="window.prismix.cmd({action:'save_scene'})">SAVE SCENE</button>
              <button type="button" class="hw-btn touch active" onclick="window.prismix.cmd({action:'load_scene'})">LOAD SCENE</button>
            </div>
          </div>
        </div>`;
      } else if (store.systemSubMode === 2) {
        const system = store.systemStatus || {};
        const security = system.security || {};
        const network = system.network || {};
        const power = system.power || {};
        const access = system.web_access || {};
        const loaded = Boolean(store.systemStatus);
        const configured = Boolean(security.configured ?? store.security.configured);
        const lanRequested = Boolean(security.lan_access ?? store.security.lan_access);
        const bind = access.bind_address || store.security.bind_address || "";
        const bindText = bind ? `${bind}${access.port || store.security.port ? ":" + (access.port || store.security.port) : ""}` : "—";
        const lanActive = lanRequested && bind && bind !== "127.0.0.1" && bind !== "localhost";
        const url = access.url || "";
        const localOnly = access.local_only !== false;
        const powerAvailable = Boolean(power.available);
        const nmAvailable = Boolean(network.network_manager);
        const wifiHardware = Boolean(network.wifi_hardware);
        const sessionTtl = security.session_remaining ?? store.security.session_remaining;
        const audioMode = sys.audio_mode === 1 ? "REAL AUDIO" : "SIMULATION";
        const engineState = sys.service_state === 2 ? "STREAMING" :
          sys.service_state === 1 ? "INITIALIZING" :
          sys.service_state === 3 ? "STOPPING" :
          (sys.sim_running ? "SIM RUNNING" : "STOPPED");
        const networkState = !loaded ? "QUERYING" : (!nmAvailable ? "UNAVAILABLE" : (network.connected ? "CONNECTED" : (network.device_state ? String(network.device_state).toUpperCase() : "DISCONNECTED")));
        const wifiLabel = !loaded ? "QUERYING" : (!wifiHardware ? "NO HARDWARE" : (network.wifi && network.ssid ? network.ssid : (network.wifi ? "NOT CONNECTED" : "ETHERNET / OTHER")));
        const signal = network.signal_percent;
        const sessionLabel = !configured ? "NO PIN SET" : formatRemaining(sessionTtl);
        const lanLabel = lanActive ? "ENABLED" : (lanRequested ? "REBIND PENDING" : "LOOPBACK ONLY");
        const webState = !url ? "UNAVAILABLE" : (localOnly ? "LOCALHOST ONLY" : "LAN ACCESS");

        contentHtml = `<div class="system-console-grid">
          <div class="hw-panel">
            <div class="hw-panel-head">
              <span class="hw-panel-title">SYSTEM POWER</span>
              ${systemBadge(powerAvailable ? "SYSTEMD READY" : "UNAVAILABLE", powerAvailable ? "ok" : "err")}
            </div>
            <div class="system-status-grid">
              <div class="system-status-cell"><span class="system-status-label">OS INTEGRATION</span><span class="system-status-value">${esc(power.integration || "SYSTEMD")}</span></div>
              <div class="system-status-cell"><span class="system-status-label">AUDIO ENGINE</span><span class="system-status-value">${esc(audioMode)} / ${esc(engineState)}</span></div>
              <div class="system-status-cell"><span class="system-status-label">RESTART</span><span class="system-status-value">${powerAvailable && power.restart !== false ? "AVAILABLE" : "UNAVAILABLE"}</span></div>
              <div class="system-status-cell"><span class="system-status-label">SHUTDOWN</span><span class="system-status-value">${powerAvailable && power.shutdown !== false ? "AVAILABLE" : "UNAVAILABLE"}</span></div>
            </div>
            <div class="system-action-row">
              <button class="hw-btn touch" type="button" ${powerAvailable && power.restart !== false ? "" : "disabled"} onclick="window.prismix.openPowerDialog('restart')">RESTART DEVICE</button>
              <button class="hw-btn touch" type="button" ${powerAvailable && power.shutdown !== false ? "" : "disabled"} onclick="window.prismix.openPowerDialog('shutdown')">SHUT DOWN</button>
            </div>
            <p class="system-note ${powerAvailable ? "" : "warn"}">${powerAvailable
              ? "Each request requires a separate confirmation. PRISMIX stops the audio engine before asking systemd to act; the OS may reject the request if the service lacks privilege."
              : esc(power.reason || "System power management is unavailable on this device.")}</p>
          </div>

          <div class="hw-panel">
            <div class="hw-panel-head">
              <span class="hw-panel-title">ACCESS &amp; SECURITY</span>
              ${systemBadge(configured ? "PIN PROTECTED" : "PIN NOT SET", configured ? "ok" : "warn")}
            </div>
            <div class="system-status-grid">
              <div class="system-status-cell"><span class="system-status-label">MIXER LOCK</span><span class="system-status-value">${configured ? (security.locked ? "LOCKED" : "UNLOCKED") : "NOT AVAILABLE"}</span></div>
              <div class="system-status-cell"><span class="system-status-label">SESSION</span><span class="system-status-value">${esc(sessionLabel)}</span></div>
              <div class="system-status-cell"><span class="system-status-label">LAN ACCESS</span><span class="system-status-value">${esc(lanLabel)}</span></div>
              <div class="system-status-cell"><span class="system-status-label">TRANSPORT</span><span class="system-status-value">${esc((access.scheme || "http").toUpperCase())} / ${access.transport_secure ? "TLS" : "NO TLS"}</span></div>
            </div>
            <div class="system-action-row">
              <button class="hw-btn touch" type="button" onclick="window.prismix.openPinDialog()">${configured ? "CHANGE PIN" : "SET UP PIN"}</button>
              <button class="hw-btn touch" type="button" ${configured ? "" : "disabled"} onclick="window.prismix.openLockDialog()">LOCK MIXER</button>
            </div>
            <div class="system-action-row">
              <button class="hw-btn touch ${lanRequested ? "danger" : ""}" type="button" ${configured ? "" : "disabled"} onclick="window.prismix.openLanAccessDialog()">${lanRequested ? "DISABLE LAN ACCESS" : "ENABLE LAN ACCESS"}</button>
              <button class="hw-btn touch" type="button" ${configured ? "" : "disabled"} onclick="window.prismix.openLogoutDialog()">SIGN OUT</button>
            </div>
            <p class="system-note ${configured ? "" : "warn"}">${configured
              ? "The mixer lock is enforced by the backend. Audio, DSP, channel state and the current selection remain untouched. LAN access uses HTTP only; enable it only on a trusted network."
              : "Set a 6 to 12 digit PIN to enable mixer lock, authenticated Web access and LAN publishing. PINs are stored only as a salted password hash."}</p>
          </div>

          <div class="hw-panel">
            <div class="hw-panel-head">
              <span class="hw-panel-title">NETWORK</span>
              ${systemBadge(loaded ? (nmAvailable ? "NETWORKMANAGER" : "NM UNAVAILABLE") : "QUERYING", !loaded ? "" : (nmAvailable ? "ok" : "err"))}
            </div>
            <div class="system-status-grid">
              <div class="system-status-cell"><span class="system-status-label">INTERFACE</span><span class="system-status-value mono">${esc(network.interface || "—")}</span></div>
              <div class="system-status-cell"><span class="system-status-label">STATE</span><span class="system-status-value">${esc(networkState)}</span></div>
              <div class="system-status-cell"><span class="system-status-label">WI-FI</span><span class="system-status-value">${esc(wifiLabel)}</span></div>
              <div class="system-status-cell"><span class="system-status-label">SIGNAL</span><span class="system-status-value">${signal >= 0 ? signalBars(signal) + " " + esc(String(signal)) + "%" : "—"}</span></div>
              <div class="system-status-cell"><span class="system-status-label">IPv4</span><span class="system-status-value mono">${esc(network.ipv4 || "—")}</span></div>
              <div class="system-status-cell"><span class="system-status-label">IPv6</span><span class="system-status-value mono">${esc(network.ipv6 || "—")}</span></div>
            </div>
            <div class="system-action-row">
              <button class="hw-btn touch" type="button" onclick="window.prismix.refreshSystem()">REFRESH STATUS</button>
              <button class="hw-btn touch" type="button" ${loaded && nmAvailable && wifiHardware ? "" : "disabled"} onclick="window.prismix.openWifiDialog()">SCAN WI-FI</button>
            </div>
            <p class="system-note ${network.error ? "warn" : ""}">${network.error ? esc(network.error) : (wifiHardware ? "Wi-Fi state and signal are read from NetworkManager. Credentials are never returned to the browser." : "No Wi-Fi hardware was detected; Wi-Fi actions stay unavailable rather than showing fabricated networks.")}</p>
          </div>

          <div class="hw-panel">
            <div class="hw-panel-head">
              <span class="hw-panel-title">WEB UI ACCESS</span>
              ${systemBadge(webState, url ? (localOnly ? "warn" : "ok") : "err")}
            </div>
            <div class="system-web-layout">
              <div>
                <div class="system-url-box">
                  <span class="system-url" id="webAccessUrl">${esc(url || "NO WEB UI ADDRESS AVAILABLE")}</span>
                  <button class="hw-btn touch" type="button" ${url ? "" : "disabled"} onclick="window.prismix.copyWebUrl()">COPY</button>
                </div>
                <div class="system-status-grid" style="margin-top:8px;">
                  <div class="system-status-cell"><span class="system-status-label">BIND</span><span class="system-status-value mono">${esc(bindText)}</span></div>
                  <div class="system-status-cell"><span class="system-status-label">MODE</span><span class="system-status-value">${esc(lanActive ? "LAN" : "LOCALHOST")}</span></div>
                  <div class="system-status-cell"><span class="system-status-label">URL SOURCE</span><span class="system-status-value">RUNTIME</span></div>
                  <div class="system-status-cell"><span class="system-status-label">TLS</span><span class="system-status-value">${access.transport_secure ? "ENABLED" : "NOT AVAILABLE"}</span></div>
                </div>
                <div class="system-action-row">
                  <button class="hw-btn touch" type="button" ${url ? "" : "disabled"} onclick="window.prismix.openQrDialog()">ENLARGE QR</button>
                  <button class="hw-btn touch" type="button" onclick="window.prismix.refreshSystem()">REFRESH ADDRESS</button>
                </div>
                <p class="system-note ${lanRequested && !lanActive ? "warn" : ""}">${lanRequested && !lanActive
                  ? "LAN access is enabled but the listener is still loopback. The rebind is pending or failed; use Refresh Address and check the bind value."
                  : "This address comes from the live server binding and active interface, not a hardcoded host. The Web UI is served over HTTP and has no TLS transport on this build."}</p>
              </div>
              <div class="system-qr-frame" id="webAccessQrFrame">
                <canvas id="webAccessQrCanvas" role="img" aria-label="QR code for the PRISMIX Web UI address"></canvas>
              </div>
            </div>
          </div>
        </div>`;
      } else {
        contentHtml = `<div class="hw-panel">
          <div class="hw-panel-head">
            <span class="hw-panel-title">UI THEME PRESET</span>
            <span class="param-val">${esc(theme.preset_name)}</span>
          </div>
          <div style="display:flex;gap:6px;flex-wrap:wrap;">
            ${themePresets.map((pName, pIdx) => `<button type="button" class="hw-btn ${theme.active_preset === pIdx ? "active" : ""}"
                    onclick="window.prismix.cmd({action:'set_theme_preset',preset:${pIdx}})">${pName}</button>`).join("")}
          </div>
          <div class="param-grid" style="margin-top:10px;">
            ${Object.entries(theme.colors || {}).map(([key, hex], rIdx) => `<div class="param-cell">
              <div class="param-head"><span class="param-lbl">${esc(key.replace("_", " "))}</span></div>
              <div style="display:flex;gap:6px;align-items:center;">
                <input type="color" value="${esc(hex)}" style="width:32px;height:32px;background:none;border:none;cursor:pointer;"
                       onchange="window.prismix.cmd({action:'set_theme_color',role:${rIdx},hex:this.value})">
                <span class="param-val" style="font-size:11px;">${esc(String(hex).toUpperCase())}</span>
              </div>
            </div>`).join("")}
          </div>
        </div>`;
      }

      ws.innerHTML = `<div class="screen-pad">
        <div class="screen-toolbar">
          <div class="toolbar-group">
            <button type="button" class="hw-btn ${store.systemSubMode === 0 ? "active" : ""}" onclick="window.prismix.setSystemSubMode(0)">AUDIO ENGINE &amp; HARDWARE</button>
            <button type="button" class="hw-btn ${store.systemSubMode === 2 ? "active" : ""}" onclick="window.prismix.setSystemSubMode(2)">SYSTEM &amp; NETWORK</button>
            <button type="button" class="hw-btn ${store.systemSubMode === 1 ? "active" : ""}" onclick="window.prismix.setSystemSubMode(1)">APPEARANCE &amp; THEME</button>
          </div>
        </div>
        ${contentHtml}
      </div>`;
      if (store.systemSubMode === 2) {
        requestAnimationFrame(drawSystemQr);
      }
    }

    function renderActiveView() {
      const ws = document.getElementById("mainWorkspace");
      const st = store.state;
      if (!ws) return;

      document.querySelectorAll("#mainNav .nav-tab").forEach(tab => {
        const isActive = Number(tab.dataset.view) === store.activeView;
        tab.classList.toggle("active", isActive);
        if (isActive) tab.setAttribute("aria-current", "page");
        else tab.removeAttribute("aria-current");
      });

      if (!st) {
        ws.innerHTML = `<div class="screen-pad">
          <div class="hw-panel engine-state-empty">
            <span class="hw-panel-title">WAITING FOR LIVE MIXER STATE</span>
            <p style="max-width:420px;color:var(--text-secondary);text-align:center;">PRISMIX channel strips, meters, and controls will appear when the audio engine connection is available.</p>
          </div>
        </div>`;
        return;
      }

      const ch = getSelectedChannelObj();
      switch (store.activeView) {
        case 0: renderMixerView(ws, st); break;
        case 1: renderChannelView(ws, st, ch); break;
        case 2: renderEqView(ws, st, ch); break;
        case 3: renderDynamicsView(ws, st, ch); break;
        case 4: renderFxView(ws, st, ch); break;
        case 5: renderRoutingView(ws, st, ch); break;
        case 6: renderSystemView(ws, st); break;
      }
      setupInteractivePointers();
      syncRangeFills(ws);
      updateLiveVisualizations();
    }

    function updateLiveVisualizations() {
      const st = store.state;
      const tel = store.telemetry;
      if (!st) return;

      // Update mixer strip level meters
      document.querySelectorAll("[data-meter-canvas]").forEach(canvas => {
        const id = Number(canvas.dataset.meterCanvas);
        const ch = getChannelForId(id);
        if (ch) drawStripMeterCanvas(canvas, ch, getTelemetryForId(id));
      });

      // Update CLIP LEDs
      document.querySelectorAll("[data-clip-led]").forEach(led => {
        const id = Number(led.dataset.clipLed);
        const t = getTelemetryForId(id);
        const ch = getChannelForId(id);
        const isClip = Boolean((t && t.clip) || (ch && ch.clip));
        led.classList.toggle("hot", isClip);
      });

      document.querySelectorAll("[data-strip-gr]").forEach(el => {
        const ch = getChannelForId(el.dataset.stripGr);
        updateGrDisplay(el, ch ? getStripGrReading(ch) : { available: false, reason: "CHANNEL UNAVAILABLE" });
      });
      document.querySelectorAll("[data-dyn-gr]").forEach(el => {
        const ch = getChannelForId(el.dataset.dynGrChannel);
        updateGrDisplay(el, ch
          ? getProcessorGrReading(ch, el.dataset.dynGr)
          : { available: false, reason: "CHANNEL UNAVAILABLE" });
      });
      document.querySelectorAll("[data-work-gr]").forEach(el => {
        const ch = getChannelForId(el.dataset.workGrChannel);
        updateWorkGrBadge(el, ch
          ? getProcessorGrReading(ch, el.dataset.workGr)
          : { available: false, reason: "CHANNEL UNAVAILABLE" });
      });

      const selCh = getSelectedChannelObj();
      if (!selCh) return;

      const eqCanvas = document.getElementById("eqRtaCanvas");
      if (eqCanvas) drawEqAndRtaCanvas(eqCanvas, selCh, tel ? tel.rta : null);

      const mixerEqCanvas = document.getElementById("mixerEqPreview");
      if (mixerEqCanvas) drawEqAndRtaCanvas(mixerEqCanvas, selCh, tel ? tel.rta : null);

      const gateCanvas = document.getElementById("gateCurveCanvas");
      if (gateCanvas) drawGateCurveCanvas(gateCanvas, selCh, getTelemetryForId(selCh.id));

      const compCanvas = document.getElementById("compCurveCanvas");
      if (compCanvas) drawCompCurveCanvas(compCanvas, selCh, getTelemetryForId(selCh.id));

    }

    // Touch & Pointer Interaction Wireup
    function setupInteractivePointers() {
      // 1. Precision Vertical Console Fader Dragging
      document.querySelectorAll("[data-fader='strip']").forEach(track => {
        const chId = Number(track.dataset.ch);
        const cap = track.querySelector(".v-fader-cap");
        let startY = 0;
        let startVal = 0.86;

        const applyVal = v => {
          v = clamp(v, 0, 1);
          track.dataset.val = String(v);
          if (cap) cap.style.top = `calc(14px + ${(1 - v).toFixed(4)} * (100% - 28px))`;
          const lbl = document.querySelector(`[data-fader-lbl="${chId}"]`);
          if (lbl) lbl.textContent = formatFaderDb(v);
          const workLbl = document.querySelector(`[data-work-fader-lbl="${chId}"]`);
          if (workLbl) workLbl.textContent = formatFaderDb(v);
          sendCoalescedCommand(`fader_${chId}`, { action: "set_fader", channel: chId, value: v });
        };

        track.onpointerdown = e => {
          e.preventDefault();
          track.setPointerCapture(e.pointerId);
          track.classList.add("dragging");
          startY = e.clientY;
          startVal = Number(track.dataset.val || 0.86);
          store.activePointerKey = `fader_${chId}`;
        };
        track.onpointermove = e => {
          if (!track.classList.contains("dragging")) return;
          const rect = track.getBoundingClientRect();
          const usableH = Math.max(10, rect.height - 28);
          const dy = startY - e.clientY;
          applyVal(startVal + dy / usableH);
        };
        const endFader = async () => {
          if (!track.classList.contains("dragging")) return;
          track.classList.remove("dragging");
          const key = store.activePointerKey;
          store.activePointerKey = null;
          if (key) await flushCoalescedCommand(key);
        };
        track.onpointerup = endFader;
        track.onpointercancel = endFader;
        track.ondblclick = () => applyVal(0.86); // Double click: Unity (0.0 dB)
      });

      // 2. Machined Rotary Knob Dragging
      document.querySelectorAll("[data-rotary]").forEach(dial => {
        const chId = Number(dial.dataset.ch);
        const kind = dial.dataset.rotary;
        const defVal = Number(dial.dataset.default || 0.5);
        const ptr = dial.querySelector(".rotary-pointer");
        let startY = 0;
        let startVal = 0.5;

        const applyVal = v => {
          v = clamp(v, 0, 1);
          dial.dataset.val = String(v);
          if (ptr) ptr.style.transform = `rotate(${(v * 270 - 135).toFixed(1)}deg)`;
          if (kind === "gain") {
            const isAux = (chId >= 101 && chId <= 104);
            const lbl = document.querySelector(`[data-gain-lbl="${chId}"]`);
            if (lbl) lbl.textContent = isAux ? formatFaderDb(v) : formatGainDb(v);
            sendCoalescedCommand(`gain_${chId}`, { action: isAux ? "set_return_gain" : "set_gain", channel: chId, value: v });
          } else {
            const lbl = document.querySelector(`[data-pan-lbl="${chId}"]`);
            if (lbl) lbl.textContent = formatPan(v);
            const workLbl = document.querySelector(`[data-work-pan-lbl="${chId}"]`);
            if (workLbl) workLbl.textContent = formatPan(v);
            sendCoalescedCommand(`pan_${chId}`, { action: "set_pan", channel: chId, value: v });
          }
        };

        dial.onpointerdown = e => {
          e.preventDefault();
          dial.setPointerCapture(e.pointerId);
          dial.classList.add("dragging");
          startY = e.clientY;
          startVal = Number(dial.dataset.val || defVal);
          store.activePointerKey = `${kind}_${chId}`;
        };
        dial.onpointermove = e => {
          if (!dial.classList.contains("dragging")) return;
          const dy = startY - e.clientY;
          applyVal(startVal + dy / 130.0);
        };
        const endRotary = async () => {
          if (!dial.classList.contains("dragging")) return;
          dial.classList.remove("dragging");
          const key = store.activePointerKey;
          store.activePointerKey = null;
          if (key) await flushCoalescedCommand(key);
        };
        dial.onpointerup = endRotary;
        dial.onpointercancel = endRotary;
        dial.ondblclick = () => applyVal(defVal);
      });

      // 3. Logarithmic Interactive EQ Nodes
      const eqCanvas = document.getElementById("eqRtaCanvas");
      if (eqCanvas) {
        const updateFromPointer = e => {
          const ch = getSelectedChannelObj();
          if (!ch || ch.geq_mode_active) return;
          const rect = eqCanvas.getBoundingClientRect();
          const padL = 36, padR = 14, padT = 14, padB = 22;
          const plotW = Math.max(10, rect.width - padL - padR);
          const plotH = Math.max(10, rect.height - padT - padB);
          const px = e.clientX - rect.left;
          const py = e.clientY - rect.top;

          if (store.draggingEqBand < 0) {
            let bestB = store.activeEqBand;
            let bestDist = 9999;
            for (let b = 0; b < 4; b++) {
              const nx = padL + freqToNorm(ch.eq_freq[b]) * plotW;
              const ny = padT + ((18.0 - ch.eq_gain[b]) / 36.0) * plotH;
              const d = Math.hypot(px - nx, py - ny);
              if (d < bestDist) { bestDist = d; bestB = b; }
            }
            store.draggingEqBand = bestB;
            store.activeEqBand = bestB;
          }

          const b = store.draggingEqBand;
          const normX = clamp((px - padL) / plotW, 0, 1);
          const normY = clamp((py - padT) / plotH, 0, 1);
          const newFreq = normToFreq(normX);
          const newGain = clamp(18.0 - normY * 36.0, -18.0, 18.0);

          ch.eq_freq[b] = newFreq;
          ch.eq_gain[b] = newGain;
          const fLbl = document.querySelector(`[data-eq-freq-lbl="${b}"]`);
          const gLbl = document.querySelector(`[data-eq-gain-lbl="${b}"]`);
          if (fLbl) fLbl.textContent = formatHz(newFreq);
          if (gLbl) gLbl.textContent = (newGain >= 0 ? "+" : "") + newGain.toFixed(1) + " dB";
          updateLiveVisualizations();
          sendCoalescedCommand(`peq_${ch.id}_${b}`, {
            action: "set_peq_band", channel: ch.id, band: b, freq: newFreq, gain: newGain
          });
        };

        eqCanvas.onpointerdown = e => {
          const ch = getSelectedChannelObj();
          if (!ch || ch.geq_mode_active) return;
          e.preventDefault();
          eqCanvas.setPointerCapture(e.pointerId);
          store.draggingEqBand = -1;
          updateFromPointer(e);
          store.activePointerKey = `peq_${ch.id}_${store.draggingEqBand}`;
        };
        eqCanvas.onpointermove = e => {
          if (store.draggingEqBand < 0) return;
          updateFromPointer(e);
        };
        const endEqDrag = async () => {
          if (store.draggingEqBand < 0) return;
          store.draggingEqBand = -1;
          const key = store.activePointerKey;
          store.activePointerKey = null;
          if (key) await flushCoalescedCommand(key);
        };
        eqCanvas.onpointerup = endEqDrag;
        eqCanvas.onpointercancel = endEqDrag;
      }
    }

    // Modal Browser Rendering
    function renderFxModalBody() {
      const body = document.getElementById("fxModalBody");
      const pag = document.getElementById("fxModalPagination");
      if (!body || !pag) return;

      document.querySelectorAll("#fxModalTabs .modal-tab").forEach(btn => {
        btn.classList.toggle("active", btn.dataset.cat === store.modalCategory);
      });

      const q = store.modalSearch.trim().toLowerCase();
      const ch = getSelectedChannelObj();
      const chId = ch ? ch.id : 1;

      if (store.modalCategory === "PRESETS") {
        const list = (store.catalog.presets || []).filter(p => !q || p.name.toLowerCase().includes(q) || p.description.toLowerCase().includes(q));
        pag.innerHTML = `<span class="param-lbl">${list.length} RACK PRESETS</span>`;
        body.innerHTML = `<div class="catalog-grid">
          ${list.map(pr => `<div class="catalog-card">
            <div>
              <div style="font-weight:700;font-size:12px;">${esc(pr.name)}</div>
              <div class="param-lbl">${esc(pr.author)} · ${pr.slots_count} SLOTS</div>
            </div>
            <div class="catalog-desc">${esc(pr.description)}</div>
            <button type="button" class="hw-btn touch active" onclick="window.prismix.applyPreset(${chId}, '${esc(pr.name)}')">LOAD PRESET</button>
          </div>`).join("")}
        </div>`;
        return;
      }

      if (store.modalCategory === "PATCHSTORAGE") {
        const ps = store.catalog.patchstorage || { items: [], current_page: 1, total_pages: 1, total_items: 0 };
        const items = ps.items || [];
        pag.innerHTML = `<span class="param-lbl">PAGE ${ps.current_page} OF ${ps.total_pages} (${ps.total_items} PATCHES)</span>
          <button class="hw-btn" type="button" ${ps.current_page <= 1 ? "disabled" : ""} onclick="window.prismix.browsePatchStorage(${ps.current_page - 1})">PREV</button>
          <button class="hw-btn" type="button" ${ps.current_page >= ps.total_pages ? "disabled" : ""} onclick="window.prismix.browsePatchStorage(${ps.current_page + 1})">NEXT</button>`;

        body.innerHTML = `<div class="catalog-grid">
          ${items.map(it => `<div class="catalog-card">
            <div>
              <div style="font-weight:700;font-size:12px;">${esc(it.title)}</div>
              <div class="param-lbl">${esc(it.author ? it.author.name : "COMMUNITY")} · V${esc(it.version)}</div>
            </div>
            <div class="catalog-desc">${esc(it.description || "LV2 plugin for PRISMIX")}</div>
            <button type="button" class="hw-btn touch ${it.status === 2 ? "green-on" : "active"}"
                    onclick="window.prismix.installPatchStorage(${it.id})">
              ${it.status === 2 ? "INSTALLED" : (it.status === 1 ? "DOWNLOADING..." : "INSTALL LV2")}
            </button>
          </div>`).join("")}
        </div>`;
        return;
      }

      let plugins = store.catalog.plugins || [];
      if (store.modalCategory === "BUILTIN") plugins = plugins.filter(p => p.source_type === 0);
      else if (store.modalCategory === "LV2") plugins = plugins.filter(p => p.source_type === 1);
      else if (store.modalCategory === "NAM") plugins = plugins.filter(p => p.source_type === 2);

      if (q) plugins = plugins.filter(p => p.name.toLowerCase().includes(q) || p.category.toLowerCase().includes(q));

      pag.innerHTML = `<span class="param-lbl">${plugins.length} PLUGINS AVAILABLE</span>`;
      body.innerHTML = `<div class="catalog-grid">
        ${plugins.map(p => `<div class="catalog-card">
          <div>
            <div style="font-weight:700;font-size:12.5px;">${esc(p.name)}</div>
            <div class="param-lbl">${esc(p.category)} · ${p.author || "AudioCore"}</div>
          </div>
          <div class="catalog-desc">${esc(p.description || "Real-time DSP insert effect")}</div>
          <button type="button" class="hw-btn touch active" onclick="window.prismix.insertPlugin(${chId}, '${esc(p.uri)}')">INSERT PLUGIN</button>
        </div>`).join("")}
      </div>`;
    }

    let channelDialogSession = null;

    function getChannelById(channelId) {
      if (!store.state) return null;
      if (Number(channelId) === 0) return store.state.master || null;
      return (store.state.channels || []).find(ch => Number(ch.id) === Number(channelId)) ||
        (store.state.aux_buses || []).find(aux => Number(aux.id) === Number(channelId)) || null;
    }

    function nextChannelName() {
      const usedIds = new Set((store.state?.channels || []).map(ch => Number(ch.id)));
      let nextId = 1;
      while (usedIds.has(nextId)) nextId++;
      return "CH " + String(nextId).padStart(2, "0");
    }

    function validateChannelName(rawName) {
      const name = String(rawName || "").trim();
      if (!name) return { value: "", error: "Enter a channel name." };
      if (new TextEncoder().encode(name).length > 64) {
        return { value: name, error: "Channel names can contain up to 64 UTF-8 bytes." };
      }
      if (name.includes("..") || /[\/\\]/.test(name) || /[\u0000-\u001F\u007F]/.test(name)) {
        return { value: name, error: 'Names cannot contain "..", slashes, or control characters.' };
      }
      return { value: name, error: "" };
    }

    function setChannelDialogError(message) {
      const error = document.getElementById("channelDialogError");
      if (!error) return;
      error.textContent = message || "";
      error.hidden = !message;
    }

    function openChannelDialog(mode, channelId = null) {
      if (!store.state || channelDialogSession) return;
      const target = channelId == null ? null : getChannelById(channelId);
      if (mode !== "add" && !target) return;
      if (mode === "delete" && (Number(channelId) <= 0 || Number(channelId) >= 100)) return;
      if (mode === "rename" && Number(channelId) === 0) return;

      const session = {
        mode,
        targetId: target ? Number(target.id) : null,
        targetName: target ? String(target.name || "") : "",
        invoker: document.activeElement,
        busy: false
      };
      channelDialogSession = session;

      const title = document.getElementById("channelDialogTitle");
      const context = document.getElementById("channelDialogContext");
      const body = document.getElementById("channelDialogBody");
      const footer = document.getElementById("channelDialogFooter");
      const backdrop = document.getElementById("channelDialogBackdrop");
      const shell = document.getElementById("consoleShell");

      if (mode === "rename") {
        title.textContent = "RENAME CHANNEL";
        context.textContent = "CHANNEL " + String(target.id).padStart(2, "0") + " · " + session.targetName;
        body.innerHTML = `<label class="channel-dialog-field">
          <span class="param-lbl">CHANNEL NAME</span>
          <input class="hw-input" id="channelDialogName" type="text" maxlength="64" autocomplete="off" spellcheck="false" required value="${esc(session.targetName)}" aria-label="New channel name">
        </label>`;
        footer.innerHTML = `<button class="hw-btn" type="button" data-dialog-cancel>CANCEL</button>
          <button class="hw-btn active" type="submit" data-dialog-submit>SAVE</button>`;
      } else if (mode === "add") {
        title.textContent = "ADD CHANNEL";
        context.textContent = "NEW LOGICAL INPUT · INPUT SOURCE STARTS UNASSIGNED";
        body.innerHTML = `<label class="channel-dialog-field" for="channelDialogFormat">
            <span class="param-lbl">CHANNEL FORMAT</span>
            <select class="hw-select" id="channelDialogFormat" aria-label="Channel format">
              <option value="0" selected>MONO INPUT</option>
              <option value="1">STEREO INPUT</option>
            </select>
          </label>
          <label class="channel-dialog-field" for="channelDialogName">
            <span class="param-lbl">CHANNEL NAME</span>
            <input class="hw-input" id="channelDialogName" type="text" maxlength="64" autocomplete="off" spellcheck="false" required value="${esc(nextChannelName())}" aria-label="Channel name">
          </label>
          <div class="channel-dialog-note">Patch a physical input from the Channel view after creation.</div>`;
        footer.innerHTML = `<button class="hw-btn" type="button" data-dialog-cancel>CANCEL</button>
          <button class="hw-btn active" type="submit" data-dialog-submit>ADD CHANNEL</button>`;
      } else if (mode === "delete") {
        title.textContent = "DELETE CHANNEL";
        context.textContent = "CHANNEL " + String(target.id).padStart(2, "0") + " · " + session.targetName;
        body.innerHTML = `<div class="channel-dialog-warning">This removes the logical mixer channel and clears its input assignment. The master and AUX buses are protected.</div>`;
        footer.innerHTML = `<button class="hw-btn" type="button" data-dialog-cancel>CANCEL</button>
          <button class="hw-btn danger" type="submit" data-dialog-submit>DELETE</button>`;
      } else {
        channelDialogSession = null;
        return;
      }

      setChannelDialogError("");
      backdrop.classList.add("open");
      backdrop.setAttribute("aria-hidden", "false");
      document.getElementById("channelDialog").setAttribute("aria-busy", "false");
      document.body.classList.add("channel-dialog-open");
      if (shell) shell.inert = true;
      const focusTarget = mode === "delete"
        ? footer.querySelector("[data-dialog-cancel]")
        : document.getElementById("channelDialogName");
      if (focusTarget) focusTarget.focus();
      else document.getElementById("channelDialog").focus();
    }

    function closeChannelDialog() {
      const session = channelDialogSession;
      if (!session || session.busy) return;
      channelDialogSession = null;
      const backdrop = document.getElementById("channelDialogBackdrop");
      const shell = document.getElementById("consoleShell");
      backdrop.classList.remove("open");
      backdrop.setAttribute("aria-hidden", "true");
      document.getElementById("channelDialog").setAttribute("aria-busy", "false");
      document.body.classList.remove("channel-dialog-open");
      if (shell) shell.inert = false;

      let restore = session.invoker && session.invoker.isConnected && !session.invoker.disabled
        ? session.invoker : null;
      if (!restore && session.targetId != null) {
        const targetStrip = Array.from(document.querySelectorAll(".channel-strip"))
          .find(strip => Number(strip.dataset.stripId) === session.targetId);
        if (targetStrip && targetStrip.getClientRects().length > 0) restore = targetStrip.querySelector(".strip-head");
      }
      if (!restore) {
        const selected = document.querySelector(".channel-strip.selected .strip-head") ||
          document.querySelector(".nav-tab.active");
        restore = selected;
      }
      if (restore && typeof restore.focus === "function") restore.focus({ preventScroll: true });
    }

    function setChannelDialogBusy(busy) {
      if (!channelDialogSession) return;
      channelDialogSession.busy = busy;
      const dialog = document.getElementById("channelDialog");
      dialog.setAttribute("aria-busy", String(busy));
      dialog.querySelectorAll("button, input, select").forEach(control => { control.disabled = busy; });
      const submit = dialog.querySelector("[data-dialog-submit]");
      if (submit && busy) {
        channelDialogSession.submitLabel = submit.textContent;
        submit.textContent = "WORKING...";
      } else if (submit && channelDialogSession.submitLabel) {
        submit.textContent = channelDialogSession.submitLabel;
        channelDialogSession.submitLabel = "";
      }
    }

    async function submitChannelDialog(event) {
      event.preventDefault();
      const session = channelDialogSession;
      if (!session || session.busy) return;

      let payload;
      if (session.mode === "rename" || session.mode === "add") {
        const nameInput = document.getElementById("channelDialogName");
        const checkedName = validateChannelName(nameInput ? nameInput.value : "");
        if (checkedName.error) {
          setChannelDialogError(checkedName.error);
          if (nameInput) nameInput.focus();
          return;
        }
        if (session.mode === "rename") {
          payload = { action: "rename_channel", channel: session.targetId, name: checkedName.value };
        } else {
          const format = Number(document.getElementById("channelDialogFormat")?.value);
          payload = { action: "add_channel", name: checkedName.value, format: format === 1 ? 1 : 0 };
        }
      } else if (session.mode === "delete") {
        if (!getChannelById(session.targetId) || session.targetId <= 0 || session.targetId >= 100) {
          setChannelDialogError("This channel is no longer available to delete.");
          return;
        }
        payload = { action: "delete_channel", channel: session.targetId };
      } else {
        return;
      }

      setChannelDialogError("");
      setChannelDialogBusy(true);
      const result = await sendCommand(payload);
      if (channelDialogSession !== session) return;
      if (!result || !result.success) {
        setChannelDialogBusy(false);
        const fallback = session.mode === "rename" ? "Could not rename this channel." :
          (session.mode === "add" ? "Could not add this channel." : "Could not delete this channel.");
        setChannelDialogError(result && result.error ? result.error : fallback);
        const focusTarget = document.getElementById("channelDialogName") ||
          document.getElementById("channelDialogFormat") || document.querySelector("#channelDialog [data-dialog-submit]");
        if (focusTarget) focusTarget.focus();
        return;
      }
      channelDialogSession.busy = false;
      closeChannelDialog();
    }

    function handleChannelDialogKeydown(event) {
      const session = channelDialogSession;
      if (!session) return;
      if (event.key === "Escape") {
        event.preventDefault();
        event.stopPropagation();
        closeChannelDialog();
        return;
      }
      if (event.key !== "Tab") return;
      const dialog = document.getElementById("channelDialog");
      const focusable = Array.from(dialog.querySelectorAll(
        'button:not(:disabled), input:not(:disabled), select:not(:disabled), [tabindex]:not([tabindex="-1"])'
      )).filter(element => element.getClientRects().length > 0);
      if (!focusable.length) {
        event.preventDefault();
        dialog.focus();
        return;
      }
      const first = focusable[0];
      const last = focusable[focusable.length - 1];
      if (event.shiftKey && (document.activeElement === first || !dialog.contains(document.activeElement))) {
        event.preventDefault();
        last.focus();
      } else if (!event.shiftKey && (document.activeElement === last || !dialog.contains(document.activeElement))) {
        event.preventDefault();
        first.focus();
      }
    }

    function installChannelDialogEvents() {
      const form = document.getElementById("channelDialogForm");
      const footer = document.getElementById("channelDialogFooter");
      form.addEventListener("submit", submitChannelDialog);
      footer.addEventListener("click", event => {
        if (event.target.closest("[data-dialog-cancel]")) closeChannelDialog();
      });
      document.addEventListener("keydown", handleChannelDialogKeydown, true);
    }

    // ------------------------------------------------------------------
    // System Console workflows
    // ------------------------------------------------------------------
    let wifiDialogToken = 0;

    function securityConfigured() {
      const sysSecurity = store.systemStatus && store.systemStatus.security;
      return Boolean(sysSecurity && typeof sysSecurity.configured === "boolean"
        ? sysSecurity.configured : store.security.configured);
    }

    function pinFieldMarkup({ id, label, autocomplete = "new-password", autofocus = false, hint = "" }) {
      return `<div class="system-field">
        <label for="${esc(id)}">${esc(label)}</label>
        <div class="system-pin-wrap">
          <input class="system-pin-input" id="${esc(id)}" type="password" inputmode="numeric"
                 pattern="[0-9]*" maxlength="12" minlength="6" autocomplete="${esc(autocomplete)}"
                 ${autofocus ? "autofocus" : ""} spellcheck="false" required>
          <button class="system-pin-reveal" type="button" data-pin-reveal="${esc(id)}" aria-label="Show PIN">SHOW</button>
        </div>
        ${hint ? `<span class="system-field-hint">${esc(hint)}</span>` : ""}
      </div>`;
    }

    function readPinField(id) {
      const el = document.getElementById(id);
      return el ? String(el.value || "") : "";
    }

    function openPinDialog() {
      const configured = securityConfigured();
      const body = configured
        ? `<p class="system-dialog-warning">Changing the PIN replaces the stored salted hash and invalidates every current Web session, including this one. A new session is issued after the change.</p>
           ${pinFieldMarkup({ id: "pinCurrent", label: "CURRENT PIN", autocomplete: "current-password", autofocus: true })}
           ${pinFieldMarkup({ id: "pinNew", label: "NEW PIN", hint: "Use 6 to 12 digits. Avoid obvious sequences." })}
           ${pinFieldMarkup({ id: "pinConfirm", label: "CONFIRM NEW PIN" })}`
        : `<p style="margin:0 0 12px;color:var(--text-secondary);font-size:11px;line-height:1.55;">
             A PIN enables the mixer lock, authenticated Web access and LAN publishing. PRISMIX stores only a salted, memory-hard password hash.
           </p>
           ${pinFieldMarkup({ id: "pinNew", label: "NEW PIN", autofocus: true, hint: "Use 6 to 12 digits." })}
           ${pinFieldMarkup({ id: "pinConfirm", label: "CONFIRM PIN" })}
           <p class="system-dialog-warning">There is no PIN reset over the network. Keep a local recovery path: an operator with OS access can remove the security configuration file.</p>`;

      openSystemModal({
        title: configured ? "CHANGE CONSOLE PIN" : "SET UP CONSOLE PIN",
        context: configured ? "ACCESS & SECURITY" : "FIRST-TIME SECURITY",
        body,
        actionButtons: [
          { label: "CANCEL", action: "cancel" },
          { label: configured ? "CHANGE PIN" : "SET PIN", action: "primary", kind: "active" }
        ],
        actions: {
          cancel: () => closeSystemModal(),
          primary: async () => {
            const currentPin = configured ? readPinField("pinCurrent") : "";
            const newPin = readPinField("pinNew");
            const confirmPin = readPinField("pinConfirm");
            setSystemModalError("");
            if (!pinLooksValid(newPin)) {
              setSystemModalError("Use 6 to 12 digits.");
              return;
            }
            if (newPin !== confirmPin) {
              setSystemModalError("The PIN confirmation does not match.");
              return;
            }
            setSystemModalBusy(true, configured ? "CHANGING..." : "SAVING...");
            const path = configured ? "/api/security/pin" : "/api/auth/setup";
            const payload = configured
              ? { current_pin: currentPin, new_pin: newPin, confirm_pin: confirmPin }
              : { pin: newPin, confirm_pin: confirmPin };
            const { ok, data } = await apiPost(path, payload);
            setSystemModalBusy(false);
            if (!ok) {
              setSystemModalError((data && data.error) || "The PIN could not be saved.");
              return;
            }
            closeSystemModal();
            await fetchSecurityStatus();
            await refreshSystemStatus(true);
            if (store.activeView === 6 && store.systemSubMode === 2) renderActiveView();
            showToast(configured ? "Console PIN changed. Other sessions were signed out." : "Console PIN configured.", "ok");
          }
        }
      });
    }

    function openUnlockDialog() {
      if (store.systemModalOpen) return;
      if (!store.security.configured) {
        showToast("No mixer PIN is configured.", "err");
        return;
      }
      openSystemModal({
        title: "UNLOCK MIXER",
        context: "BACKEND PIN VERIFICATION",
        body: `<p style="margin:0 0 12px;color:var(--text-secondary);font-size:11px;line-height:1.55;">
                 The lock does not stop audio or alter DSP state. Enter the console PIN to restore local and Web control.
               </p>
               ${pinFieldMarkup({ id: "unlockPin", label: "CONSOLE PIN", autocomplete: "current-password", autofocus: true })}
               <p class="system-dialog-warning">Repeated failures are temporarily rate limited by the security service.</p>`,
        actionButtons: [
          { label: "CANCEL", action: "cancel" },
          { label: "UNLOCK", action: "primary", kind: "active" }
        ],
        actions: {
          cancel: () => closeSystemModal(),
          primary: async () => {
            const pin = readPinField("unlockPin");
            setSystemModalError("");
            if (!pinLooksValid(pin)) {
              setSystemModalError("Enter the 6 to 12 digit console PIN.");
              return;
            }
            setSystemModalBusy(true, "VERIFYING...");
            const { ok, data } = await apiPost("/api/auth/unlock", { pin }, false);
            setSystemModalBusy(false);
            if (!ok) {
              setSystemModalError((data && data.error) || "PIN verification failed.");
              return;
            }
            store.security.locked = false;
            store.security.authenticated = true;
            closeSystemModal(false);
            clearSecurityGates();
            await enterConsole(true);
            showToast("Mixer unlocked. Audio and mix state were not interrupted.", "ok");
          }
        }
      });
    }

    function openLockDialog() {
      if (!securityConfigured()) {
        showToast("Set a console PIN before locking the mixer.", "err");
        return;
      }
      openSystemModal({
        title: "LOCK MIXER",
        context: "AUDIO CONTINUES",
        body: `<div class="system-dialog-warning">
                 Locking blocks local and Web controls immediately. Audio processing, mute states, DSP parameters, the current channel selection and all channel state remain unchanged.
               </div>
               <p class="system-note" style="margin-top:12px;">This browser session will be signed out. Return with the console PIN to unlock. The lock state is persisted by the security service.</p>`,
        actionButtons: [
          { label: "CANCEL", action: "cancel" },
          { label: "LOCK MIXER", action: "primary", kind: "danger" }
        ],
        actions: {
          cancel: () => closeSystemModal(),
          primary: async () => {
            setSystemModalBusy(true, "LOCKING...");
            const { ok, data } = await apiPost("/api/security/lock", {});
            setSystemModalBusy(false);
            if (!ok) {
              setSystemModalError((data && data.error) || "The mixer could not be locked.");
              return;
            }
            store.security.locked = true;
            store.security.authenticated = false;
            showLockGate();
            showToast("Mixer locked. Audio continues unchanged.", "ok");
          }
        }
      });
    }

    function openLogoutDialog() {
      openSystemModal({
        title: "SIGN OUT OF WEB UI",
        context: "CURRENT BROWSER SESSION",
        body: `<p style="margin:0;color:var(--text-secondary);font-size:11px;line-height:1.55;">
                 This ends the authenticated browser session. Audio and mixer state continue running. You will need the console PIN to sign in again.
               </p>`,
        actionButtons: [
          { label: "CANCEL", action: "cancel" },
          { label: "SIGN OUT", action: "primary" }
        ],
        actions: {
          cancel: () => closeSystemModal(),
          primary: async () => {
            setSystemModalBusy(true, "SIGNING OUT...");
            const { ok, data } = await apiPost("/api/security/logout", {});
            setSystemModalBusy(false);
            if (!ok) {
              setSystemModalError((data && data.error) || "The session could not be ended.");
              return;
            }
            closeSystemModal(false);
            store.security.authenticated = false;
            store.state = null;
            store.telemetry = null;
            stopSse();
            showAuthGate();
            showToast("Web session ended.", "ok");
          }
        }
      });
    }

    function openPowerDialog(operation) {
      const isRestart = operation === "restart";
      const label = isRestart ? "RESTART DEVICE" : "SHUT DOWN DEVICE";
      openSystemModal({
        title: label,
        context: "SYSTEM POWER",
        body: `<div class="system-dialog-danger">
                 PRISMIX will stop the audio engine, then ask systemd to ${isRestart ? "restart" : "power off"} the device. This interrupts audio output and disconnects every browser.
               </div>
               <p class="system-note" style="margin-top:12px;">
                 The operating system may reject the request if this service lacks the required privilege. PRISMIX will show the actual error instead of claiming the device changed state.
               </p>`,
        actionButtons: [
          { label: "CANCEL", action: "cancel" },
          { label: isRestart ? "RESTART NOW" : "SHUT DOWN NOW", action: "primary", kind: "danger" }
        ],
        actions: {
          cancel: () => closeSystemModal(),
          primary: async () => {
            setSystemModalError("");
            setSystemModalBusy(true, "REQUESTING...");
            const { ok, data, status } = await apiPost("/api/system/power", {
              operation,
              confirm: true
            });
            if (ok && data && data.accepted) {
              setSystemModalBusy(false);
              setSystemModalBody(`<div class="system-dialog-success">
                  <strong>REQUEST ACCEPTED</strong><br>
                  systemd accepted the ${isRestart ? "restart" : "power-off"} request. The device may restart or power off shortly.
                </div>
                <p class="system-note">This is an accepted pending request, not a claim that the device has already changed state.</p>`);
              document.getElementById("systemModalFooter").innerHTML =
                `<button class="hw-btn touch" type="button" data-sm-action="close">CLOSE</button>`;
              store.systemModalActions = { close: () => closeSystemModal() };
              return;
            }
            setSystemModalBusy(false);
            const message = data && data.error
              ? data.error
              : (status === 0 ? "No confirmation was received from the device." : "The power request failed.");
            setSystemModalError(message);
            await refreshSystemStatus(true);
            await fetchState();
          }
        }
      });
    }

    function openLanAccessDialog() {
      if (!securityConfigured()) {
        showToast("Set a console PIN before enabling LAN access.", "err");
        return;
      }
      const currentlyEnabled = Boolean(store.security.lan_access);
      const enabling = !currentlyEnabled;
      openSystemModal({
        title: enabling ? "ENABLE LAN ACCESS" : "DISABLE LAN ACCESS",
        context: "WEB UI EXPOSURE",
        body: `<div class="system-dialog-warning">
                 ${enabling
                   ? "This publishes the Web UI on the active LAN address of this device. The service uses unencrypted HTTP and has no TLS transport on this build. PIN authentication is still required for every protected API, but network observers can read traffic."
                   : "Remote browsers will lose access to the Web UI when the listener returns to loopback. The local Web UI and audio engine remain running."}
               </div>
               <p class="system-note" style="margin-top:12px;">
                 Bind only on a trusted network. This setting is persisted by the shared security service and applies to the live HTTP listener.
               </p>`,
        actionButtons: [
          { label: "CANCEL", action: "cancel" },
          { label: enabling ? "ENABLE LAN ACCESS" : "DISABLE LAN ACCESS", action: "primary", kind: enabling ? "danger" : "active" }
        ],
        actions: {
          cancel: () => closeSystemModal(),
          primary: async () => {
            setSystemModalBusy(true, "APPLYING...");
            const { ok, data } = await apiPost("/api/security/lan", { enabled: enabling });
            setSystemModalBusy(false);
            if (!ok) {
              setSystemModalError((data && data.error) || "LAN access could not be changed.");
              return;
            }
            store.security.lan_access = enabling;
            closeSystemModal(false);
            showToast(enabling
              ? "LAN access enabled. Refreshing the live bind address."
              : "LAN access disabled. Returning the listener to loopback.", "ok");
            setTimeout(async () => {
              await new Promise(resolve => setTimeout(resolve, 600));
              await refreshSystemStatus(true);
              await fetchSecurityStatus();
            }, 0);
          }
        }
      });
    }

    async function loadWifiNetworks(force = false) {
      const token = ++wifiDialogToken;
      let current = document.getElementById("wifiDialogBody");
      if (!current) return;
      current.innerHTML = `<div class="system-dialog-readout">SCANNING NETWORKMANAGER FOR WI-FI NETWORKS…</div>`;
      const deadline = Date.now() + 30000;
      while (Date.now() < deadline) {
        const query = force ? "?refresh=1" : "";
        force = false;
        const { ok, data } = await apiGet("/api/system/networks" + query);
        if (token !== wifiDialogToken || !store.systemModalOpen) return;
        current = document.getElementById("wifiDialogBody");
        if (!current) return;
        if (!ok || !data) {
          current.innerHTML = `<div class="system-dialog-danger">${esc((data && data.error) || "Wi-Fi scan failed.")}</div>`;
          return;
        }
        if (data.scanning) {
          current.innerHTML = `<div class="system-dialog-readout">SCANNING NETWORKMANAGER… This can take a few seconds.</div>`;
          await new Promise(resolve => setTimeout(resolve, 800));
          continue;
        }
        if (!data.success) {
          current.innerHTML = `<div class="system-dialog-warning">${esc(data.error || "Wi-Fi is unavailable.")}</div>`;
          return;
        }
        const networks = Array.isArray(data.networks) ? data.networks : [];
        if (!networks.length) {
          current.innerHTML = `<div class="system-dialog-readout">No Wi-Fi networks were found.</div>`;
          return;
        }
        current.innerHTML = `<div class="system-wifi-list">${networks.map(network => `
          <button class="system-wifi-row ${network.active ? "active" : ""}" type="button"
                  data-wifi-ssid="${esc(network.ssid)}"
                  data-wifi-secured="${network.secured ? "1" : "0"}"
                  data-wifi-active="${network.active ? "1" : "0"}">
            <span>
              <span class="system-wifi-ssid">${esc(network.ssid)}</span>
              <span class="system-wifi-meta">
                <span>${network.active ? "ACTIVE" : (network.secured ? esc(network.security || "SECURED") : "OPEN")}</span>
                <span>${Number(network.signal_percent) >= 0 ? esc(String(network.signal_percent)) + "%" : "SIGNAL —"}</span>
              </span>
            </span>
            <span style="display:flex;align-items:center;gap:7px;">
              ${signalBars(network.signal_percent)}
              ${network.active ? systemBadge("ACTIVE", "ok") : ""}
            </span>
          </button>`).join("")}</div>
          <p class="system-note">Credentials are sent only to the local NetworkManager service and are never returned to the browser.</p>`;
        return;
      }
      current = document.getElementById("wifiDialogBody");
      if (current) {
        current.innerHTML = `<div class="system-dialog-warning">The Wi-Fi scan did not finish in time. Close this dialog and try Refresh again.</div>`;
      }
    }

    function openWifiDialog() {
      wifiDialogToken += 1;
      openSystemModal({
        title: "WI-FI NETWORKS",
        context: "NETWORKMANAGER",
        body: `<div id="wifiDialogBody"><div class="system-dialog-readout">REQUESTING A LIVE SCAN…</div></div>`,
        actionButtons: [
          { label: "REFRESH", action: "refresh" },
          { label: "CLOSE", action: "cancel" }
        ],
        actions: {
          refresh: () => loadWifiNetworks(true),
          cancel: () => { wifiDialogToken += 1; closeSystemModal(); }
        },
        afterOpen: () => loadWifiNetworks(false)
      });
    }

    function chooseWifiNetwork(ssid, secured, active) {
      if (active) {
        showToast("This network is already active.", "ok");
        return;
      }
      openSystemModal({
        title: "CONNECT TO WI-FI",
        context: secured ? "SECURED NETWORK" : "OPEN NETWORK",
        body: `<div class="system-dialog-warning">
                 Changing Wi-Fi can interrupt this Web UI if the device's LAN address changes. The PRISMIX audio engine runs locally and is not stopped by this action.
               </div>
               <div class="system-dialog-readout"><strong>${esc(ssid)}</strong><br>${secured ? "WPA / WPA2 / WPA3 protected" : "Open network — no password"}</div>
               ${secured ? `<div class="system-field" style="margin-top:12px;">
                  <label for="wifiPassword">WI-FI PASSWORD</label>
                  <input class="system-input" id="wifiPassword" type="password" maxlength="63"
                         autocomplete="off" spellcheck="false" autofocus required>
                  <span class="system-field-hint">8 to 63 printable characters. The password is not stored by PRISMIX.</span>
                </div>` : ""}`,
        actionButtons: [
          { label: "BACK", action: "back" },
          { label: "CONNECT", action: "primary", kind: "active" }
        ],
        actions: {
          back: () => openWifiDialog(),
          primary: async () => {
            const passwordEl = document.getElementById("wifiPassword");
            const password = passwordEl ? String(passwordEl.value || "") : "";
            if (secured && (password.length < 8 || password.length > 63)) {
              setSystemModalError("Enter the Wi-Fi password (8 to 63 characters).");
              return;
            }
            setSystemModalError("");
            setSystemModalBusy(true, "CONNECTING...");
            const { ok, data } = await apiPost("/api/system/wifi/connect", { ssid, password });
            setSystemModalBusy(false);
            if (!ok) {
              setSystemModalError((data && data.error) || "NetworkManager could not activate this network.");
              return;
            }
            closeSystemModal(false);
            showToast("NetworkManager confirmed the new Wi-Fi connection.", "ok");
            setTimeout(async () => {
              await new Promise(resolve => setTimeout(resolve, 500));
              await refreshSystemStatus(true);
              await fetchSecurityStatus();
            }, 0);
          }
        }
      });
    }

    function openQrDialog() {
      const url = store.systemStatus && store.systemStatus.web_access
        ? store.systemStatus.web_access.url : "";
      if (!url) {
        showToast("No Web UI address is available for a QR code.", "err");
        return;
      }
      openSystemModal({
        title: "WEB UI QR ACCESS",
        context: (store.systemStatus.web_access.local_only ? "LOCALHOST" : "LAN"),
        body: `<div class="system-qr-modal"><canvas id="systemQrModalCanvas" role="img" aria-label="QR code for ${esc(url)}"></canvas></div>
               <div class="system-url-box"><span class="system-url">${esc(url)}</span></div>
               <p class="system-note">This QR code contains the exact runtime URL shown in Web UI Access. It does not include a PIN or session token.</p>`,
        actionButtons: [
          { label: "COPY URL", action: "copy" },
          { label: "CLOSE", action: "cancel" }
        ],
        actions: {
          copy: () => copyTextToClipboard(url),
          cancel: () => closeSystemModal()
        },
        afterOpen: () => {
          if (!drawQrToCanvas("systemQrModalCanvas", url)) {
            setSystemModalError("The QR renderer could not encode this address.");
          }
        }
      });
    }

    async function refreshSystemConsole(showFeedback = true) {
      const data = await refreshSystemStatus(true);
      if (showFeedback) {
        showToast(data ? "System status refreshed from the backend." : "System status is unavailable.",
                  data ? "ok" : "err");
      }
      return data;
    }

    async function copyWebUrl() {
      const url = store.systemStatus && store.systemStatus.web_access
        ? store.systemStatus.web_access.url : "";
      return copyTextToClipboard(url);
    }

    function installSystemConsoleEvents() {
      const loginForm = document.getElementById("loginForm");
      const loginSubmit = document.getElementById("loginSubmit");
      if (loginForm) {
        loginForm.addEventListener("submit", async event => {
          event.preventDefault();
          const input = document.getElementById("loginPin");
          const error = document.getElementById("loginError");
          const pin = input ? String(input.value || "") : "";
          if (error) { error.textContent = ""; error.hidden = true; }
          if (input) input.setAttribute("aria-invalid", "false");
          if (!pinLooksValid(pin)) {
            if (error) { error.textContent = "Enter the 6 to 12 digit console PIN."; error.hidden = false; }
            if (input) { input.setAttribute("aria-invalid", "true"); input.focus(); }
            return;
          }
          if (loginSubmit) { loginSubmit.disabled = true; loginSubmit.textContent = "VERIFYING..."; }
          const { ok, status, data } = await apiPost("/api/auth/login", { pin }, false);
          if (loginSubmit) { loginSubmit.disabled = false; loginSubmit.textContent = "UNLOCK CONSOLE"; }
          if (!ok) {
            if (status === 423) {
              showLockGate();
              return;
            }
            const message = (data && data.error) || "PIN verification failed.";
            if (error) { error.textContent = message; error.hidden = false; }
            if (input) { input.value = ""; input.setAttribute("aria-invalid", "true"); input.focus(); }
            return;
          }
          if (input) { input.value = ""; input.setAttribute("aria-invalid", "false"); }
          store.security.authenticated = true;
          store.security.locked = false;
          await fetchSecurityStatus();
          if (store.authGateActive) {
            await enterConsole();
          }
          showToast("Console unlocked.", "ok");
        });
      }

      document.addEventListener("click", event => {
        const reveal = event.target.closest("[data-pin-reveal]");
        if (!reveal) return;
        const input = document.getElementById(reveal.dataset.pinReveal);
        if (!input) return;
        const showing = input.type === "text";
        input.type = showing ? "password" : "text";
        reveal.textContent = showing ? "SHOW" : "HIDE";
        reveal.setAttribute("aria-label", showing ? "Show PIN" : "Hide PIN");
        reveal.setAttribute("aria-pressed", showing ? "false" : "true");
      });

      const footer = document.getElementById("systemModalFooter");
      if (footer) {
        footer.addEventListener("click", event => {
          const button = event.target.closest("[data-sm-action]");
          if (!button) return;
          runSystemModalAction(button.dataset.smAction);
        });
      }
      const modalBody = document.getElementById("systemModalBody");
      if (modalBody) {
        modalBody.addEventListener("click", event => {
          const row = event.target.closest("[data-wifi-ssid]");
          if (!row) return;
          chooseWifiNetwork(row.dataset.wifiSsid, row.dataset.wifiSecured === "1",
                            row.dataset.wifiActive === "1");
        });
      }
      const backdrop = document.getElementById("systemModalBackdrop");
      if (backdrop) {
        backdrop.addEventListener("mousedown", event => {
          if (event.target === backdrop && !store.systemModalBusy) closeSystemModal();
        });
      }
      const lockGate = document.getElementById("mixerLockGate");
      if (lockGate) lockGate.addEventListener("keydown", handleLockGateKeydown);
      document.addEventListener("keydown", handleSystemModalKeydown, true);
    }

    // Public Application Controller Object
    window.prismix = {
      setView(v) {
        store.activeView = v;
        renderActiveView();
        if (v === 6 && store.systemSubMode === 2) refreshSystemStatus(true);
      },
      selectChannel(id) {
        if (id === 0) {
          store.state.selected_channel = -1;
          store.state.selected_aux_bus = -1;
        } else if (id >= 101 && id <= 104) {
          store.state.selected_channel = -2;
          store.state.selected_aux_bus = id - 101;
        } else {
          const idx = (store.state.channels || []).findIndex(c => c.id === id);
          store.state.selected_channel = (idx >= 0) ? idx : 0;
          store.state.selected_aux_bus = -1;
        }
        sendCommand({ action: "select_channel", channel: id });
        renderShellStatus();
        renderChannelSelectorStrip();
        renderActiveView();
      },
      jumpToProc(chId, viewId) {
        window.prismix.selectChannel(chId);
        window.prismix.setView(viewId);
      },
      async toggleMute(chId, muted) {
        await sendCommand({ action: "set_mute", channel: chId, value: muted });
      },
      async toggleSolo(chId, soloed) {
        await sendCommand({ action: "set_solo", channel: chId, value: soloed });
      },
      async toggleAudioEngine() {
        const sys = (store.state && store.state.system) || {};
        const isSim = (sys.audio_mode === 0);
        const isRunning = isSim ? Boolean(store.telemetry?.sim_running ?? sys.sim_running) : (sys.service_state === 2);
        await sendCommand({ action: isRunning ? "stop_audio" : "start_audio" });
      },
      openAudioSetup() {
        store.systemSubMode = 0;
        store.activeView = 6;
        renderActiveView();
      },
      addChannel() {
        openChannelDialog("add");
      },
      deleteChannel(chId) {
        openChannelDialog("delete", chId);
      },
      promptRenameChannel(chId) {
        openChannelDialog("rename", chId);
      },
      cmd(payload) {
        sendCommand(payload);
      },
      coalesce(key, payload) {
        sendCoalescedCommand(key, payload);
      },
      beginWorkControl(key) {
        store.activePointerKey = key;
      },
      async endWorkControl(key) {
        if (store.activePointerKey === key) store.activePointerKey = null;
        await flushCoalescedCommand(key);
      },
      fmtFader: formatFaderDb,
      fmtGain: formatGainDb,
      fmtPan: formatPan,
      selectEqBand(b) {
        store.activeEqBand = b;
        renderActiveView();
      },
      onPeqSlider(chId, b, field, rawVal) {
        const ch = getSelectedChannelObj();
        if (!ch) return;
        store.activeEqBand = b;
        const payload = { action: "set_peq_band", channel: chId, band: b };
        let valueText = "";
        if (field === "freq") {
          const hz = normToFreq(Number(rawVal));
          ch.eq_freq[b] = hz;
          payload.freq = hz;
          valueText = formatHz(hz);
          const lbl = document.querySelector(`[data-eq-freq-lbl="${b}"]`);
          if (lbl) lbl.textContent = valueText;
        } else if (field === "gain") {
          const g = Number(rawVal);
          ch.eq_gain[b] = g;
          payload.gain = g;
          valueText = (g >= 0 ? "+" : "") + g.toFixed(1) + " dB";
          const lbl = document.querySelector(`[data-eq-gain-lbl="${b}"]`);
          if (lbl) lbl.textContent = valueText;
        } else if (field === "q") {
          const q = Number(rawVal);
          ch.eq_q[b] = q;
          payload.q = q;
          valueText = q.toFixed(2);
          const lbl = document.querySelector(`[data-eq-q-lbl="${b}"]`);
          if (lbl) lbl.textContent = valueText;
        }
        const range = document.querySelector(`[data-eq-range="${field}_${b}"]`);
        if (range && valueText) range.setAttribute("aria-valuetext", valueText);
        updateLiveVisualizations();
        sendCoalescedCommand(`peq_${chId}_${b}_${field}`, payload);
      },
      onGateParam(chId, paramId, val) {
        const ch = getSelectedChannelObj();
        const num = Number(val);
        if (ch) {
          if (paramId === "thresh") ch.gate_thresh = num;
          if (paramId === "range") ch.gate_range = num;
        }
        updateLiveVisualizations();
        sendCoalescedCommand(`gate_${chId}_${paramId}`, { action: "set_gate_param", channel: chId, param_id: paramId, value: num });
      },
      onCompParam(chId, paramId, val) {
        const ch = getSelectedChannelObj();
        const num = Number(val);
        if (ch) {
          if (paramId === "thresh") ch.comp_thresh = num;
          if (paramId === "ratio") ch.comp_ratio = num;
          if (paramId === "knee") ch.comp_knee = num;
          if (paramId === "makeup") ch.comp_makeup = num;
        }
        updateLiveVisualizations();
        sendCoalescedCommand(`comp_${chId}_${paramId}`, { action: "set_comp_param", channel: chId, param_id: paramId, value: num });
      },
      onLimParam(chId, paramId, val) {
        const ch = getSelectedChannelObj();
        const num = Number(val);
        if (ch && paramId === "ceiling") ch.limiter_ceiling_db = num;
        updateLiveVisualizations();
        sendCoalescedCommand(`lim_${chId}_${paramId}`, { action: "set_limiter_param", channel: chId, param_id: paramId, value: num });
      },
      selectFxSlot(s) {
        store.activeFxSlot = s;
        renderActiveView();
      },
      openFxModal() {
        store.modalOpen = true;
        document.getElementById("fxModalBackdrop").classList.add("open");
        fetchCatalog();
        renderFxModalBody();
      },
      closeFxModal() {
        store.modalOpen = false;
        document.getElementById("fxModalBackdrop").classList.remove("open");
      },
      setModalCategory(cat) {
        store.modalCategory = cat;
        if (cat === "PATCHSTORAGE") {
          window.prismix.browsePatchStorage(1);
        }
        renderFxModalBody();
      },
      onModalSearch(val) {
        store.modalSearch = val || "";
        renderFxModalBody();
      },
      async insertPlugin(chId, uri) {
        await sendCommand({ action: "add_plugin", channel: chId, uri });
        window.prismix.closeFxModal();
      },
      async applyPreset(chId, presetName) {
        await sendCommand({ action: "apply_preset", channel: chId, preset: presetName });
        window.prismix.closeFxModal();
      },
      promptSavePreset(chId) {
        openSystemModal({
          title: "SAVE CHANNEL PRESET",
          context: "PRESET LIBRARY",
          body: `<div class="system-field">
              <label for="presetName">PRESET NAME</label>
              <input class="system-input" id="presetName" type="text" maxlength="64"
                     autocomplete="off" spellcheck="false" value="CustomChain" autofocus>
              <span class="system-field-hint">Saved to the existing PRISMIX preset library for this channel.</span>
            </div>`,
          actionButtons: [
            { label: "CANCEL", action: "cancel" },
            { label: "SAVE PRESET", action: "primary", kind: "active" }
          ],
          actions: {
            cancel: () => closeSystemModal(),
            primary: async () => {
              const input = document.getElementById("presetName");
              const name = input ? String(input.value || "").trim() : "";
              if (!name) {
                setSystemModalError("Enter a preset name.");
                return;
              }
              setSystemModalBusy(true, "SAVING...");
              const result = await sendCommand({ action: "save_preset", channel: chId, preset: name }, false);
              setSystemModalBusy(false);
              if (!result || !result.success) {
                setSystemModalError((result && result.error) || "The preset could not be saved.");
                return;
              }
              closeSystemModal();
              showToast("Preset saved.", "ok");
            }
          }
        });
      },
      async browsePatchStorage(page) {
        store.modalPage = Math.max(1, page || 1);
        await sendCommand({ action: "patchstorage_browse", page: store.modalPage, query: store.modalSearch }, false);
        setTimeout(fetchCatalog, 350);
      },
      async installPatchStorage(patchId) {
        await sendCommand({ action: "patchstorage_install", patch_id: patchId }, false);
        setTimeout(fetchCatalog, 500);
      },
      setRoutingSubMode(m) {
        store.routingSubMode = m;
        renderActiveView();
      },
      setSystemSubMode(m) {
        store.systemSubMode = m;
        renderActiveView();
        if (m === 2) refreshSystemStatus(true);
      },
      openPinDialog,
      openLockDialog,
      openUnlockDialog,
      openLogoutDialog,
      openPowerDialog,
      openLanAccessDialog,
      openWifiDialog,
      openQrDialog,
      copyWebUrl,
      refreshSystem() {
        return refreshSystemConsole(true);
      }
    };

    installChannelDialogEvents();
    installSystemConsoleEvents();

    // Server-Sent Events (SSE) Real-Time Sync. The stream is created only after
    // a session is established; errors trigger an authoritative status check
    // before any reconnect is attempted.
    let sseSource = null;
    let sseRetryTimer = null;

    function stopSse() {
      if (sseRetryTimer) {
        clearTimeout(sseRetryTimer);
        sseRetryTimer = null;
      }
      if (sseSource) {
        try { sseSource.close(); } catch (_) {}
        sseSource = null;
      }
    }

    function connectSse() {
      if (typeof EventSource === "undefined") return;
      if (store.security.configured && (!store.security.authenticated || store.security.locked)) return;
      if (store.authGateActive || sseSource) return;
      const es = new EventSource("/api/events");
      sseSource = es;
      es.onopen = () => setConnected(true);
      es.onmessage = ev => {
        try {
          const msg = JSON.parse(ev.data);
          if (msg.event === "state_changed" && !store.activePointerKey) {
            fetchState();
          } else if (msg.event === "security_changed") {
            fetchSecurityStatus();
            refreshSystemStatus(true);
          }
        } catch (_) {}
      };
      es.onerror = () => {
        if (sseSource === es) sseSource = null;
        try { es.close(); } catch (_) {}
        setConnected(false);
        fetchSecurityStatus().then(() => {
          if (store.security.configured && (!store.security.authenticated || store.security.locked)) return;
          if (store.authGateActive) return;
          if (sseRetryTimer) clearTimeout(sseRetryTimer);
          sseRetryTimer = setTimeout(() => {
            sseRetryTimer = null;
            connectSse();
          }, 2000);
        });
      };
    }

    window.addEventListener("resize", () => {
      updateLiveVisualizations();
      syncMixerDeckMetrics();
    });

    // Initial render from console default state
    renderShellStatus();
    renderChannelSelectorStrip();
    renderActiveView();

    // Bootstrap through the shared security status endpoint. The gate is decided
    // before any authenticated state, catalog or telemetry request is made.
    fetchSecurityStatus().then(status => {
      if (!status) {
        fetchState();
        fetchCatalog();
      }
    });
    setInterval(fetchSecurityStatus, 8000);
    setInterval(() => {
      if (store.activeView === 6 && store.systemSubMode === 2 &&
          (!store.security.configured || store.security.authenticated) &&
          !store.security.locked) {
        refreshSystemStatus(false);
      }
    }, 10000);
    setInterval(fetchTelemetry, 75);
  })();
  </script>
</body>
</html>
)rawhtml";
    return s_html;
}

} // namespace prismix::web
