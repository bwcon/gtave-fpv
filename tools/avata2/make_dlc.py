"""Packs the exported drone model into a dlcpack (dlcpacks/gtavefpv/dlc.rpf).

    python tools/avata2/make_dlc.py <dir with gtavefpv_avata2.ydr/.ytd/.ytyp> <out dir>

The pack streams the model from x64/gtavefpv.rpf and registers its archetype
(DLC_ITYP_REQUEST), so scripts can spawn "gtavefpv_avata2" by name. Add
<Item>dlcpacks:/gtavefpv/</Item> to dlclist.xml (onigiri: onigiri/common/data/dlclist.xml).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import rpf7  # noqa: E402

PACK = "gtavefpv"
MODEL = "gtavefpv_avata2"

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
    <Item>
      <filename>dlc_{PACK}:/%PLATFORM%/{PACK}.rpf/{MODEL}.ytyp</filename>
      <fileType>DLC_ITYP_REQUEST</fileType>
      <overlay value="false" />
      <disabled value="true" />
      <persistent value="true" />
    </Item>
  </dataFiles>
  <contentChangeSets>
    <Item>
      <changeSetName>{PACK.upper()}_AUTOGEN</changeSetName>
      <mapChangeSetData />
      <filesToInvalidate />
      <filesToDisable />
      <filesToEnable>
        <Item>dlc_{PACK}:/%PLATFORM%/{PACK}.rpf</Item>
        <Item>dlc_{PACK}:/%PLATFORM%/{PACK}.rpf/{MODEL}.ytyp</Item>
      </filesToEnable>
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
inner = rpf7.build({f"{MODEL}.{ext}": ("res", *rpf7.read_resource(os.path.join(src, f"{MODEL}.{ext}")))
                    for ext in ("ydr", "ytd", "ytyp")})
dlc = rpf7.build({"content.xml": CONTENT.encode(), "setup2.xml": SETUP.encode(), "x64": {f"{PACK}.rpf": inner}})
os.makedirs(os.path.join(out, PACK), exist_ok=True)
path = os.path.join(out, PACK, "dlc.rpf")
with open(path, "wb") as f:
    f.write(dlc)
print(path, len(dlc), "bytes")
rpf7.list_archive(path)
