"""Packs the exported drone model into a dlcpack (dlcpacks/gtavefpv/dlc.rpf).

    python tools/models/make_dlc.py <dir with the models' .ydr/.ytd/.ytyp> <out dir>

The pack streams every model in the dir from x64/gtavefpv.rpf and registers
their archetypes (DLC_ITYP_REQUEST), so scripts can spawn them by name. Add
<Item>dlcpacks:/gtavefpv/</Item> to dlclist.xml (onigiri: onigiri/common/data/dlclist.xml).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import rpf7  # noqa: E402

PACK = "gtavefpv"

CONTENT = f"""<?xml version="1.0" encoding="UTF-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <includedDataFiles />
  <dataFiles>
    <Item>
      <filename>dlc_{PACK}:/%PLATFORM%/{PACK}.rpf</filename>
      <fileType>RPF_FILE</fileType>
      <overlay value="false" />
      <disabled value="true" />
      <persistent value="true" />
    </Item>
{{ITYPS}}  </dataFiles>
  <contentChangeSets>
    <Item>
      <changeSetName>{PACK.upper()}_AUTOGEN</changeSetName>
      <mapChangeSetData />
      <filesToInvalidate />
      <filesToDisable />
      <filesToEnable>
        <Item>dlc_{PACK}:/%PLATFORM%/{PACK}.rpf</Item>
{{ENABLE}}      </filesToEnable>
      <txdToLoad />
      <txdToUnload />
      <residentResources />
      <unregisterResources />
    </Item>
  </contentChangeSets>
  <patchFiles />
</CDataFileMgr__ContentsOfDataFileXml>
"""

SETUP = f"""<?xml version="1.0" encoding="UTF-8"?>
<SSetupData>
  <deviceName>dlc_{PACK}</deviceName>
  <datFile>content.xml</datFile>
  <timeStamp>10/26/2026 00:00:00</timeStamp>
  <nameHash>{PACK}</nameHash>
  <contentChangeSetGroups>
    <Item>
      <NameHash>GROUP_STARTUP</NameHash>
      <ContentChangeSets>
        <Item>{PACK.upper()}_AUTOGEN</Item>
      </ContentChangeSets>
    </Item>
  </contentChangeSetGroups>
  <startupScript />
  <scriptCallstackSize value="0" />
  <type>EXTRACONTENT_COMPAT_PACK</type>
  <order value="999" />
  <minorOrder value="0" />
  <isLevelPack value="false" />
  <dependencyPackHash />
  <requiredVersion />
  <subPackCount value="0" />
</SSetupData>
"""

src, out = sys.argv[1], sys.argv[2]
files = sorted(f for f in os.listdir(src) if f.endswith((".ydr", ".ytd", ".ytyp")))
ITEM = """    <Item>
      <filename>dlc_{pack}:/%PLATFORM%/{pack}.rpf/{f}</filename>
      <fileType>DLC_ITYP_REQUEST</fileType>
      <overlay value="false" />
      <disabled value="true" />
      <persistent value="true" />
    </Item>
"""
ytyps = [f for f in files if f.endswith(".ytyp")]
CONTENT = CONTENT.replace("{ITYPS}", "".join(ITEM.format(pack=PACK, f=f) for f in ytyps)).replace(
    "{ENABLE}", "".join(f"        <Item>dlc_{PACK}:/%PLATFORM%/{PACK}.rpf/{f}</Item>\n" for f in ytyps))
inner = rpf7.build({f: ("res", *rpf7.read_resource(os.path.join(src, f))) for f in files})
dlc = rpf7.build({"content.xml": CONTENT.encode(), "setup2.xml": SETUP.encode(), "x64": {f"{PACK}.rpf": inner}})
os.makedirs(os.path.join(out, PACK), exist_ok=True)
path = os.path.join(out, PACK, "dlc.rpf")
with open(path, "wb") as f:
    f.write(dlc)
print(path, len(dlc), "bytes")
rpf7.list_archive(path)
