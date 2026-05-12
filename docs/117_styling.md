# UI Styling and Themes

## Intent

Arvane client UI should be configurable, consistent, and readable. The engine should avoid hard-coded visual styling in menus and screens wherever practical.

The preferred model is theme-driven:

- `style.yaml` selects the active theme and stores broad client style preferences.
- Theme assets in enabled packs define colors, accents, spacing, typography, and common UI element styling.
- UI screens consume named style tokens instead of embedding raw colors or arbitrary sizing rules.

This keeps menus consistent, makes end-user customization approachable, and allows the client to support multiple visual identities without rewriting UI code.

---

## File Layout

Client UI styling is configured through:

```text
arvane_home/
  config/
    style.yaml
  packs/
    arvane_base/
      assets/
        arvane/
          themes/
            default.yaml
            high_contrast.yaml
            dark.yaml
            light.yaml
```

`style.yaml` selects the active theme:

```yaml
theme: "default" # resolves to arvane:theme/default
```

Theme names are logical names. They should not include file extensions or path separators.

Valid examples:

```yaml
theme: "default"
theme: "dark"
theme: "high_contrast"
```

Invalid examples:

```yaml
theme: "../somewhere"
theme: "default.yaml"
theme: "themes/default.yaml"
```

The client should resolve bare theme names using the `arvane` namespace:

```text
arvane:theme/<theme>
```

A full theme resource ID such as `example_pack:theme/night` may also be used.

If the selected theme cannot be loaded, the client should fall back to `default`.

---

## `style.yaml`

`style.yaml` should remain small. It should select the theme and contain high-level user preferences that are not part of the theme itself.

Recommended shape:

```yaml
theme: "default"

ui:
  scale: 1.0
  font_scale: 1.0
  animation_scale: 1.0
```

Fields:

* `theme`: logical theme name.
* `ui.scale`: global UI scale multiplier.
* `ui.font_scale`: global text scale multiplier.
* `ui.animation_scale`: global UI animation multiplier.

The theme file should own colors, spacing, typography defaults, and widget styling. Avoid putting the complete color palette directly in `style.yaml`.

---

## Theme File

A theme file defines named style tokens.

Example:

```yaml
name: "Default"
description: "Default Arvane theme."

colors:
  background: "#101318"
  surface: "#181d24"
  surface_hover: "#202733"
  surface_pressed: "#11161d"

  text: "#f2f4f8"
  text_muted: "#a7afbd"
  text_disabled: "#697180"

  border: "#303846"
  border_focus: "#6aa6ff"

  accent: "#4f8cff"
  accent_hover: "#6aa0ff"
  accent_pressed: "#356fd1"
  accent_text: "#ffffff"

  danger: "#e05252"
  danger_hover: "#f06767"
  danger_pressed: "#b83f3f"

  warning: "#e0a84f"
  success: "#4fbf7f"

layout:
  spacing_xs: 4
  spacing_sm: 8
  spacing_md: 12
  spacing_lg: 16
  spacing_xl: 24

  radius_sm: 4
  radius_md: 8
  radius_lg: 12

  border_width: 1

typography:
  default_font: "arvane:font/default"
  heading_font: "arvane:font/default"
  monospace_font: "arvane:font/mono"

  size_sm: 12
  size_md: 16
  size_lg: 20
  size_xl: 28

widgets:
  button:
    height: 40
    min_width: 120
    padding_x: 16
    padding_y: 8
    radius: 8

  input:
    height: 40
    min_width: 220
    padding_x: 12
    padding_y: 8
    radius: 8

  panel:
    padding: 16
    radius: 12
```

---

## Color Tokens

UI code should reference named tokens instead of hard-coded colors.

Preferred:

```cpp
button.background = theme.colors.accent;
button.text_color = theme.colors.accent_text;
```

Avoid:

```cpp
button.background = Color::from_hex("#4f8cff");
button.text_color = Color::from_hex("#ffffff");
```

Raw colors are acceptable inside theme files. Runtime UI code should use theme tokens.

Recommended base color tokens:

```yaml
colors:
  background: "#101318"
  surface: "#181d24"
  surface_hover: "#202733"
  surface_pressed: "#11161d"

  text: "#f2f4f8"
  text_muted: "#a7afbd"
  text_disabled: "#697180"

  border: "#303846"
  border_focus: "#6aa6ff"

  accent: "#4f8cff"
  accent_hover: "#6aa0ff"
  accent_pressed: "#356fd1"
  accent_text: "#ffffff"

  danger: "#e05252"
  danger_hover: "#f06767"
  danger_pressed: "#b83f3f"

  warning: "#e0a84f"
  success: "#4fbf7f"
```

Additional tokens may be added when needed, but avoid creating one-off tokens for a single screen unless the visual role is reusable.

---

## Accent Colors

Accent colors represent the primary interactive identity of the current theme.

Use accent colors for:

* Primary buttons
* Focus outlines
* Selected list rows
* Active tabs
* Sliders
* Checkboxes
* Important highlights

Do not overuse the accent color. If every element is accented, nothing feels important.

Common usage:

```yaml
colors:
  accent: "#4f8cff"
  accent_hover: "#6aa0ff"
  accent_pressed: "#356fd1"
  accent_text: "#ffffff"
```

Interactive states should be explicit. Do not derive hover and pressed colors ad hoc in individual widgets unless the theme loader provides a centralized derivation rule.

---

## Layout Tokens

Spacing and sizing should be tokenized so screens feel consistent.

Recommended layout tokens:

```yaml
layout:
  spacing_xs: 4
  spacing_sm: 8
  spacing_md: 12
  spacing_lg: 16
  spacing_xl: 24

  radius_sm: 4
  radius_md: 8
  radius_lg: 12

  border_width: 1
```

UI code should use these tokens for padding, margins, gaps, borders, and rounded corners.

Avoid arbitrary layout values scattered across menu code.

Preferred:

```cpp
panel.padding = theme.layout.spacing_lg;
row.gap = theme.layout.spacing_md;
```

Avoid:

```cpp
panel.padding = 17;
row.gap = 11;
```

---

## Typography Tokens

Text should use named font and size roles.

Recommended typography tokens:

```yaml
typography:
  default_font: "arvane:font/default"
  heading_font: "arvane:font/default"
  monospace_font: "arvane:font/mono"

  size_sm: 12
  size_md: 16
  size_lg: 20
  size_xl: 28
```

Common usage:

* `size_sm`: secondary labels, hints, small metadata.
* `size_md`: normal body text and most buttons.
* `size_lg`: section titles.
* `size_xl`: screen titles.

Text should be readable at default UI scale. Avoid tiny labels for important actions.

---

## Widget Style Tokens

Common widgets should have reusable defaults.

Recommended widget section:

```yaml
widgets:
  button:
    height: 40
    min_width: 120
    padding_x: 16
    padding_y: 8
    radius: 8

  input:
    height: 40
    min_width: 220
    padding_x: 12
    padding_y: 8
    radius: 8

  panel:
    padding: 16
    radius: 12
```

Widget code should use these defaults unless the screen has a strong reason to override them.

Overrides should still use theme tokens where possible.

---

## Theme Loading

Theme loading should be strict but recoverable.

The client should:

1. Load `arvane_home/config/style.yaml`.
2. Read the selected `theme`.
3. Resolve it to a theme asset resource ID.
4. Load and validate the theme file.
5. Fall back to `default` if the selected theme is missing or invalid.
6. Fall back to built-in engine defaults if `default` is missing or invalid.

Invalid fields should be reported through logging. The client should not crash because of a malformed theme file unless running in an explicit developer validation mode.

Recommended behavior:

* Missing optional token: use default token.
* Missing required color token: use built-in fallback.
* Invalid color string: log warning and use fallback.
* Invalid numeric size: clamp to safe range or use fallback.
* Unknown fields: ignore and warn only in verbose/developer mode.

---

## Color Format

Theme colors should use hex strings. Six hex digits are fully opaque, and eight hex digits include alpha.

Supported formats:

```yaml
accent: "#4f8cff"
overlay: "#00000080"
```

The theme parser should normalize parsed colors into the engine color representation used by `arv_ui`.

---

## Runtime Reloading

The client may support theme reloads for development and settings screens.

Theme reload should:

* Reload `style.yaml`.
* Reload the selected theme file.
* Rebuild or update cached style tokens.
* Mark active UI screens dirty so they can restyle.
* Avoid destroying active menu state unless necessary.

Reloading should not require a client restart once the UI system supports it.

---

## Accessibility

Themes should preserve readability and usability.

Guidelines:

* Text must contrast clearly against its background.
* Disabled text should look disabled but remain legible.
* Focused widgets should have a visible focus state.
* Important destructive actions should use danger styling.
* Do not rely only on color to communicate state.
* Avoid extremely small clickable targets.
* Avoid overly transparent UI surfaces behind important text.

High-contrast themes should be possible using the same theme schema.

---

## General UI Design Principles for Agents

When implementing menus or client UI screens, prioritize clarity over novelty.

A good Arvane UI should be:

* easy to scan
* predictable
* readable
* responsive
* consistent with the active theme
* comfortable to use with mouse and keyboard

Do not design screens as isolated art pieces. Screens should feel like part of the same application.

---

## Layout Principles

Prefer simple, structured layouts.

Good defaults:

* Center primary menu groups when appropriate.
* Use consistent spacing between related elements.
* Group related controls into panels or sections.
* Align labels, inputs, and buttons cleanly.
* Keep the most important action visually prominent.
* Keep destructive actions visually separated from common actions.

Avoid:

* random button placement
* inconsistent gaps
* uneven alignment
* overcrowded panels
* tiny controls
* excessive nesting
* decorative elements that reduce readability

Menus should be usable at a glance.

---

## Button Placement

Button placement should match user expectations.

Recommended patterns:

* Primary action on the right or visually emphasized.
* Cancel/back action near the primary action, but less emphasized.
* Destructive action separated from normal confirmation actions.
* Back/navigation controls placed consistently across screens.
* Large menu actions stacked vertically for main navigation.

Example confirmation row:

```text
[Cancel]                         [Create]
```

Example destructive row:

```text
[Delete World]                   [Cancel] [Open World]
```

The destructive action is separated so it is harder to trigger accidentally.

---

## Button Sizing

Buttons should be large enough to read and click comfortably.

Use theme widget defaults:

```yaml
widgets:
  button:
    height: 40
    min_width: 120
```

Guidelines:

* Primary menu buttons should generally share the same width.
* Related buttons in a row should have compatible heights.
* Do not size buttons tightly around short labels if it creates visual noise.
* Do not make destructive buttons larger than the primary action unless destruction is the main purpose of the screen.

---

## Inputs and Forms

Forms should be predictable and forgiving.

Guidelines:

* Place labels near their inputs.
* Use clear placeholder text only as a hint, not as the only label.
* Use consistent input widths within a form.
* Show validation errors near the relevant field.
* Keep form confirmation buttons at the bottom.
* Preserve user-entered text when validation fails.

Avoid ambiguous forms where the user cannot tell what field is being edited.

---

## Lists and Selection

Lists should clearly show item identity, hover state, and selected state.

Guidelines:

* Selected rows should use accent or a clear selected surface token.
* Hover state should be visible but weaker than selected state.
* Empty lists should show a useful empty-state message.
* Long lists should support scrolling.
* Important item actions should be near the selected item or in a consistent action area.

Do not require the user to guess which item is currently selected.

---

## Visual Hierarchy

Every screen should have a clear hierarchy.

Recommended order:

1. Screen title.
2. Short explanation or context, if useful.
3. Main content.
4. Secondary options.
5. Navigation or confirmation actions.

Use typography and spacing to express hierarchy. Do not rely only on color.

---

## Error, Warning, and Success States

Use semantic theme tokens for status messages.

Examples:

```yaml
colors:
  danger: "#e05252"
  warning: "#e0a84f"
  success: "#4fbf7f"
```

Guidelines:

* Errors should be specific and actionable.
* Warnings should explain risk.
* Success messages should be brief.
* Destructive confirmations should clearly state what will happen.

Avoid vague errors such as:

```text
Failed.
```

Prefer:

```text
Could not create world: a world with this name already exists.
```

---

## Keyboard and Controller Navigation

Menus should not assume mouse-only interaction.

Guidelines:

* Maintain a predictable focus order.
* Show visible focus state.
* Enter/confirm should activate the focused primary control.
* Escape/back should return to the previous screen where appropriate.
* Arrow keys or tab should navigate controls consistently.

The focus ring should use the theme focus token:

```yaml
colors:
  border_focus: "#6aa6ff"
```

---

## Animation

Animations should be subtle and functional.

Good uses:

* hover transitions
* panel fade-in
* focus transitions
* selection movement
* loading indicators

Avoid:

* long transitions
* excessive bouncing
* animations that delay interaction
* animations that obscure state changes

Respect:

```yaml
ui:
  animation_scale: 1.0
```

If `animation_scale` is `0.0`, non-essential UI animation should be disabled.

---

## Agent Implementation Rules

When adding or changing UI code:

1. Do not hard-code theme colors in screens.
2. Use named theme tokens.
3. Prefer reusable widget defaults.
4. Keep layout values consistent with theme spacing tokens.
5. Make primary, secondary, and destructive actions visually distinct.
6. Preserve keyboard navigation and visible focus states.
7. Validate theme files without crashing the client.
8. Log useful warnings for invalid theme data.
9. Keep `style.yaml` small.
10. Put actual theme definitions in pack assets under `assets/<namespace>/themes/`.

Agents should not add one-off styling systems for individual menus. Extend the theme schema when a new reusable visual role is needed.

---
