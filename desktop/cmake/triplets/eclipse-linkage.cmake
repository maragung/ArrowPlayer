# Per-port linkage policy — spec §4.2 (REQ-GEN-012) and §4.3 (REQ-GEN-013).
#
# The Linkage column of the dependency register is not advisory. LGPL components
# must be replaceable by the user, which means a shared library; the register
# says "Dynamic" for each of them and this file is that column expressed as
# build configuration rather than as a sentence in a document.
#
# vcpkg evaluates a triplet once per port with PORT set to the port name, which
# is what makes per-port linkage possible at all. Every Eclipse triplet includes
# this file; none of them sets VCPKG_LIBRARY_LINKAGE itself.

# LGPL-2.1-or-later components. Dynamic linkage is a licence obligation here:
# §4.3 rule 2 requires that a user be able to swap the shared library for a
# compatible build and still run Eclipse Player.
set(_eclipse_lgpl_dynamic
    ffmpeg          # LGPL-2.1-or-later, built without --enable-gpl (§4.4)
    taglib          # LGPL-2.1-or-later OR MPL-1.1 — register says Dynamic
    soundtouch      # LGPL-2.1-or-later
    chromaprint     # LGPL-2.1-or-later
    projectm)       # LGPL-2.1-or-later

# Permissively licensed, but the register still records them as Dynamic, so the
# triplet records them as dynamic too. Divergence between the register and the
# build is the failure mode this file exists to prevent.
set(_eclipse_register_dynamic
    rtaudio)        # MIT-like, optional fallback sink only (§8.7.7)

# list(FIND) rather than IN_LIST: IN_LIST depends on policy CMP0057, and a
# triplet is included in whatever policy scope vcpkg happens to provide.
list(FIND _eclipse_lgpl_dynamic "${PORT}" _eclipse_lgpl_idx)
list(FIND _eclipse_register_dynamic "${PORT}" _eclipse_reg_idx)

if(_eclipse_lgpl_idx GREATER -1 OR _eclipse_reg_idx GREATER -1)
    set(VCPKG_LIBRARY_LINKAGE dynamic)
else()
    # Static OK per the register: sqlite3, libsamplerate, libzip, zlib, gtest.
    set(VCPKG_LIBRARY_LINKAGE static)
endif()

unset(_eclipse_lgpl_idx)
unset(_eclipse_reg_idx)

unset(_eclipse_lgpl_dynamic)
unset(_eclipse_register_dynamic)
