#!/bin/bash
#
# Verify pgraft package after build
#

set -e

PACKAGE_FILE="$1"
PKG_TYPE="$2"

if [ -z "$PACKAGE_FILE" ] || [ ! -f "$PACKAGE_FILE" ]; then
    echo "Usage: $0 <package-file> <rpm|deb>"
    exit 1
fi

echo "Verifying package: $PACKAGE_FILE"
echo ""

case "$PKG_TYPE" in
    rpm)
        echo "Package info:"
        rpm -qilp "$PACKAGE_FILE"
        echo ""
        echo "Dependencies:"
        rpm -qp --requires "$PACKAGE_FILE"
        echo ""
        echo "Files:"
        rpm -qpl "$PACKAGE_FILE" | grep -E "pgraft"
        ;;
    
    deb)
        echo "Package info:"
        dpkg -I "$PACKAGE_FILE"
        echo ""
        echo "Files:"
        dpkg -c "$PACKAGE_FILE" | grep -E "pgraft"
        ;;
    
    *)
        echo "Error: Package type must be 'rpm' or 'deb'"
        exit 1
        ;;
esac

echo ""
echo "✅ Package verification complete"

