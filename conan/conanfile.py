# Consolidated dependency graph for Orca(pnp_gui) — the single `conan install`
# that provisions every third-party C/C++ dependency in ONE coherent graph
# (ADR-0001 amendment: replaces xmake's isolated per-package installs, whose
# separate graphs resolved conflicting transitive versions — e.g. opencascade
# pulling freetype/2.13.2 beside the project's 2.12.1, cgal pulling
# boost/1.83.0 + eigen/3.4.0).
#
# Driven by xmake (rule "pnp.conan" in xmake.lua) as:
#   conan install conan/ --profile:host=conan/profile_host.txt
#       --profile:build=conan/profile_build.txt --lockfile=conan/conan.lock
#       --build=missing -of build/conan/<plat>_<arch>_<mode>
#
# generate() emits pnp_deps.lua — a Lua table with per-package cpp_info and
# direct dependency names — which the xmake rule reads (io.load) to inject
# include dirs / links / defines into targets.
#
# Version policy (ADR-0001): exact project pins where ConanCenter still serves
# them; nearest-upstream otherwise, gated on behavior/tests:
#   exact:   wxwidgets 3.3.2, boost 1.84.0, eigen 5.0.1, cereal 1.3.0,
#            draco 1.5.7, qhull 8.0.2, glfw 3.4, cgal 5.6.3, libnoise 1.0.0,
#            opencascade 7.6.0, freetype 2.12.1, openssl 1.1.1w, expat 2.8.2*
#   nearest: libcurl 7.86.0 (pin 7.75.0), onetbb 2021.7.0 (pin 2021.5.0),
#            opencv 4.5.5 (pin 4.6.0), nlopt 2.9.1 (pin 2.5.0; 2.7.1 exists on
#            center but its CMakeLists is rejected by CMake 4)
#   dropped: openvdb/opencsg/openexr/glew — only served SLA-era code that is
#            dead in this fork (OpenVDBUtils' sole consumer has no callers).
#   (*) expat: deps/ vendored version differs; 2.8.2 is what wxwidgets already
#       pulls transitively — single version keeps the graph coherent.

from conan import ConanFile


class OrcaPnpDeps(ConanFile):
    settings = "os", "arch", "compiler", "build_type"

    default_options = {
        # wxWidgets: static, WebView/Edge + private fonts (Windows-first set;
        # mirrors the proven proof configuration from the ADR)
        "wxwidgets/*:shared": False,
        "wxwidgets/*:webview": True,
        "wxwidgets/*:mediactrl": True,
        "wxwidgets/*:opengl": True,
        "wxwidgets/*:aui": True,
        "wxwidgets/*:html": True,
        "wxwidgets/*:stc": False,
        "wxwidgets/*:cairo": False,
        "wxwidgets/*:custom_enables":
            "wxUSE_PRIVATE_FONTS, wxUSE_GLCANVAS_EGL, wxUSE_WEBREQUEST, wxUSE_WEBVIEW_EDGE",
        "wxwidgets/*:custom_disables":
            "wxUSE_DETECT_SM, wxUSE_WEBVIEW_IE, wxUSE_LIBSDL, wxUSE_XTEST, "
            "wxUSE_LIBTIFF, wxUSE_NANOSVG, wxUSE_LIBWEBP",

        "boost/*:header_only": True,
        "zlib/*:shared": False,
        "libpng/*:shared": False,
        "libjpeg/*:shared": False,

        # OCCT: static everywhere (deps/ built it Shared on Windows; static is
        # the deliberate divergence — AGPL app, no DLL staging, no /MT
        # cross-heap hazard; behavior-gated). No Tk (deps/ used USE_TK=OFF).
        "opencascade/*:shared": False,
        "opencascade/*:with_tk": False,

        # OpenCV: app uses core + imgproc only (SkipPartCanvas, ObjColorUtils)
        "opencv/*:shared": False,
        "opencv/*:imgproc": True,
        "opencv/*:calib3d": False,
        "opencv/*:dnn": False,
        "opencv/*:features2d": False,
        "opencv/*:flann": False,
        "opencv/*:gapi": False,
        "opencv/*:highgui": False,
        "opencv/*:imgcodecs": False,
        "opencv/*:ml": False,
        "opencv/*:objdetect": False,
        "opencv/*:photo": False,
        "opencv/*:stitching": False,
        "opencv/*:video": False,
        "opencv/*:videoio": False,
        "opencv/*:with_eigen": False,
        "opencv/*:with_ipp": False,

        "libcurl/*:shared": False,
        "libcurl/*:with_ssl": "openssl",
        "openssl/*:shared": False,
        "onetbb/*:shared": True,  # oneTBB does not support fully static builds
        "onetbb/*:tbbmalloc": True,
        "hwloc/*:shared": True,   # onetbb's recipe rejects static hwloc
        "nlopt/*:shared": False,
        "freetype/*:shared": False,
        "expat/*:shared": False,
    }

    def requirements(self):
        self.requires("wxwidgets/3.3.2")
        self.requires("boost/1.84.0")
        self.requires("eigen/5.0.1")
        self.requires("cereal/1.3.0")
        self.requires("draco/1.5.7")
        self.requires("qhull/8.0.2")
        self.requires("glfw/3.4")
        self.requires("cgal/5.6.3")
        self.requires("libnoise/1.0.0")
        self.requires("zlib/1.3.1")
        self.requires("libpng/1.6.47")
        self.requires("libjpeg/9f")
        self.requires("expat/2.8.2")
        self.requires("nanosvg/cci.20231025")
        self.requires("opencascade/7.6.0")
        self.requires("freetype/2.12.1")
        self.requires("openssl/1.1.1w")
        self.requires("libcurl/7.86.0")
        self.requires("onetbb/2021.7.0")
        self.requires("opencv/4.5.5")
        self.requires("nlopt/2.9.1")

    # ---------------------------------------------------------------- output

    @staticmethod
    def _lua_string(value):
        return '"%s"' % str(value).replace("\\", "/").replace('"', '\\"')

    @classmethod
    def _lua_list(cls, values, indent):
        values = list(values or [])
        if not values:
            return "{}"
        pad = " " * indent
        items = (",\n" + pad).join(cls._lua_string(v) for v in values)
        return "{\n%s%s\n%s}" % (pad, items, " " * (indent - 4))

    def generate(self):
        import os
        from conan.tools.files import save

        entries = []
        for require, dep in self.dependencies.host.items():
            name = require.ref.name
            info = dep.cpp_info.aggregated_components()
            try:
                direct = [r.ref.name for r, _ in dep.dependencies.direct_host.items()]
            except Exception:
                direct = []
            fields = [
                ("version", self._lua_string(dep.ref.version)),
                ("includedirs", self._lua_list(info.includedirs, 12)),
                ("linkdirs", self._lua_list(info.libdirs, 12)),
                ("links", self._lua_list(info.libs, 12)),
                ("syslinks", self._lua_list(info.system_libs, 12)),
                ("frameworks", self._lua_list(info.frameworks, 12)),
                ("frameworkdirs", self._lua_list(info.frameworkdirs, 12)),
                ("bindirs", self._lua_list(info.bindirs, 12)),
                ("defines", self._lua_list(info.defines, 12)),
                ("cxxflags", self._lua_list(info.cxxflags, 12)),
                ("cflags", self._lua_list(info.cflags, 12)),
                ("shflags", self._lua_list(info.sharedlinkflags, 12)),
                ("ldflags", self._lua_list(info.exelinkflags, 12)),
                ("deps", self._lua_list(direct, 12)),
            ]
            body = ",\n        ".join("%s = %s" % (k, v) for k, v in fields)
            entries.append('    ["%s"] = {\n        %s\n    }' % (name, body))

        content = (
            "{\n"
            "    __settings = {\n"
            '        os = %s,\n'
            '        arch = %s,\n'
            '        build_type = %s\n'
            "    },\n"
            "%s\n"
            "}\n"
        ) % (
            self._lua_string(self.settings.os),
            self._lua_string(self.settings.arch),
            self._lua_string(self.settings.build_type),
            ",\n".join(entries),
        )
        save(self, os.path.join(self.generators_folder, "pnp_deps.lua"), content)
