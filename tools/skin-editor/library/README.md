# Skin Studio asset library

This directory is tool data, not a viewer source checkout:

- `skins/`: registered source skins, themes, XUI definitions, textures, and
  localized labels used for authoring and native exports.
- `commands.xml`: toolbar command definitions.

The data was copied without modification from
[`anne-skydancer/vulkanstorm` commit
`08c07b4cbabc70190a5a69e74057289dc6914c58`](https://github.com/anne-skydancer/vulkanstorm/commit/08c07b4cbabc70190a5a69e74057289dc6914c58):
`indra/newview/skins/` and `indra/newview/app_settings/commands.xml`.

The data is a compatibility snapshot, not a promise that every command or skin
role exists in every viewer version. Validate exported skins in the viewer that
will use them.

For a different compatible asset library, keep the same directory structure and
start the browser server with `--root C:\path\to\library`. The desktop packager
uses the library stored here.

Original copyright, artwork licence, and trademark notices are preserved in
[the adjacent licences directory](../licenses/). No additional rights to artwork
or trademarks are granted by including it in the tool.
