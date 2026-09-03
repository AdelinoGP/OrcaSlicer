--!Staging and freshness of the pnp backend dist.
--
-- SchemaBridgeMap ticket 16. `cargo xtask dist` leaves no provenance in the
-- staged tree (`pnp_cli --version` reports only the crate version, 0.1.0), and
-- the fork's wire check is major-only (PnpBackend.hpp SUPPORTED_CONFIG_SCHEMA_MAJOR),
-- so a dist built before a minor wire bump silently declares fewer keys -- the
-- failure ticket 06 hit (65 host keys dropped) and ticket 19 debugged around.
--
-- The guard is therefore fork-side and build-time: `xmake pnp` records the
-- submodule commit it staged from, and the bundle step restages when that
-- commit no longer matches. Ticket 16 chose self-heal over ticket 08's hard
-- failure for BOTH the missing and the stale case, so a C++ build may start a
-- cargo build; --pnp_bundle_cli=n is the escape valve (also the way to build
-- deliberately against an older backend).

local SUBMODULE = "pinch_n_print_cli"
local STAMP = ".pnp-stamp"

-- HEAD of the submodule working tree, or nil outside a git checkout.
function submodule_head()
    local head = try {function ()
        return os.iorunv("git", {"-C", SUBMODULE, "rev-parse", "HEAD"})
    end}
    return head and head:trim() or nil
end

-- True when the submodule has uncommitted changes. HEAD equality cannot see
-- these, and pnp-side iteration is exactly when they exist, so they warn.
function submodule_dirty()
    local out = try {function ()
        return os.iorunv("git", {"-C", SUBMODULE, "status", "--porcelain"})
    end}
    return out ~= nil and out:trim() ~= ""
end

function stamp_file(dist_dir)
    return path.join(dist_dir, STAMP)
end

-- The commit a dist was staged from, or nil when it carries no stamp. An
-- unstamped dist passes silently (ticket 16): it predates this guard or was
-- staged by bare `cargo xtask dist`, and its freshness is simply unknown.
function read_stamp(dist_dir)
    local file = stamp_file(dist_dir)
    if not os.isfile(file) then
        return nil
    end
    local content = try {function () return io.readfile(file) end}
    return content and content:trim() or nil
end

-- Run `cargo xtask dist` and stamp the result with the commit it was built
-- from. Shared by the `pnp` task and the bundle step's self-heal.
function stage(edition, debug, dist_root)
    local flags = " --edition " .. edition .. (debug and " --debug" or "")
    os.cd(SUBMODULE)
    os.exec("cargo xtask dist" .. flags)
    os.cd("-")
    local head = submodule_head()
    if head then
        local dist_dir = path.join(dist_root or path.join(SUBMODULE, "target/dist"), edition)
        if os.isdir(dist_dir) then
            io.writefile(stamp_file(dist_dir), head .. "\n")
        end
    end
end
