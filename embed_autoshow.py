"""Embed the stock purchase preview layout with its Y prompt."""

import sys
from pathlib import Path
from zipfile import ZipFile

with ZipFile(sys.argv[1]) as archive:
    layout = archive.read("Scenes/Anthem/A_CarDetails.xaml").decode("utf-8-sig")
layout = layout.replace("\r\n", "\n").replace("\r", "\n")

anchor = 'local:ControllerButtonPanel.XDescription="{local:Loc HelpButtons.IDS_ChangeCamera}"'
if layout.count(anchor) != 1 or "ControllerButtonPanel.YDescription" in layout:
    raise ValueError("Unexpected FH3 purchase-preview button bar")
layout = layout.replace(
    anchor,
    anchor + '\n                            local:ControllerButtonPanel.YDescription="Explode"',
)
Path(sys.argv[2]).write_text(
    'constexpr char kAutoshowLayout[] = R"FH3XAML(' + layout + ')FH3XAML";\n',
    encoding="utf-8",
)
