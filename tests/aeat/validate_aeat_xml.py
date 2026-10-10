"""Validate XML written by the direct AEAT client against the official AEAT schemas.

Usage: validate_aeat_xml.py <xsd dir> <xml dir>

Every *.xml file in <xml dir> is checked: a SOAP envelope is validated by the
element inside its Body, a bare record (RegistroAlta / RegistroAnulacion) by its
own element. The schemas in <xsd dir> are the files AEAT publishes, unchanged;
the W3C signature schema they import from the web is resolved to the local copy,
so the check never needs the network.

Exit codes: 0 all valid, 1 a file is invalid, 77 lxml is not installed (the
ctest entry treats 77 as skipped).
"""

import pathlib
import sys

try:
    from lxml import etree
except ImportError:
    print("lxml not installed: skipping the AEAT schema check (pip install lxml)")
    sys.exit(77)

SOAP_NS = "http://schemas.xmlsoap.org/soap/envelope/"
AEAT = "https://www2.agenciatributaria.gob.es/static_files/common/internet/dep/aplicaciones/es/aeat/tike/cont/ws/"
SCHEMA_BY_NAMESPACE = {
    AEAT + "SuministroLR.xsd": "SuministroLR.xsd",
    AEAT + "SuministroInformacion.xsd": "SuministroInformacion.xsd",
    AEAT + "ConsultaLR.xsd": "ConsultaLR.xsd",
    AEAT + "RespuestaSuministro.xsd": "RespuestaSuministro.xsd",
    AEAT + "RespuestaConsultaLR.xsd": "RespuestaConsultaLR.xsd",
}
XMLDSIG_URL = "http://www.w3.org/TR/xmldsig-core/xmldsig-core-schema.xsd"


class LocalResolver(etree.Resolver):
    def __init__(self, xsd_dir):
        super().__init__()
        self.xsd_dir = xsd_dir

    def resolve(self, url, public_id, context):
        if url == XMLDSIG_URL:
            return self.resolve_filename(str(self.xsd_dir / "xmldsig-core-schema.xsd"), context)
        return None


def load_schema(xsd_dir, name, cache):
    if name not in cache:
        parser = etree.XMLParser(no_network=True, load_dtd=False, resolve_entities=True)
        parser.resolvers.add(LocalResolver(xsd_dir))
        cache[name] = etree.XMLSchema(etree.parse(str(xsd_dir / name), parser))
    return cache[name]


def payload(root):
    if root.tag == "{%s}Envelope" % SOAP_NS:
        body = root.find("{%s}Body" % SOAP_NS)
        return body[0]
    return root


def main():
    xsd_dir = pathlib.Path(sys.argv[1])
    xml_dir = pathlib.Path(sys.argv[2])
    files = sorted(xml_dir.glob("*.xml"))
    if not files:
        print("no XML files in", xml_dir)
        return 1
    cache = {}
    failed = 0
    for path in files:
        element = payload(etree.parse(str(path)).getroot())
        namespace = etree.QName(element).namespace
        schema_name = SCHEMA_BY_NAMESPACE.get(namespace)
        if schema_name is None:
            print("FAIL", path.name, "- unknown namespace", namespace)
            failed += 1
            continue
        schema = load_schema(xsd_dir, schema_name, cache)
        if schema.validate(etree.ElementTree(element)):
            print("ok  ", path.name, "against", schema_name)
        else:
            failed += 1
            print("FAIL", path.name, "against", schema_name)
            for error in schema.error_log:
                print("     line", error.line, error.message)
    print(len(files) - failed, "of", len(files), "files valid")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
