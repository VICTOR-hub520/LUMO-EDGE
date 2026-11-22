"use strict";

const { spawnSync } = require("node:child_process");
const path = require("node:path");

const projectRoot = path.resolve(__dirname, "..");
const vcpkgRoot = process.env.VCPKG_ROOT;
if (!vcpkgRoot) {
  console.error("Set VCPKG_ROOT to your vcpkg installation before building the desktop app.");
  process.exit(1);
}

function run(command, args) {
  const result = spawnSync(command, args, {
    cwd: projectRoot,
    stdio: "inherit",
    shell: process.platform === "win32",
  });
  if (result.error) {
    console.error(`Could not run ${command}: ${result.error.message}`);
    process.exit(1);
  }
  if (result.status !== 0) process.exit(result.status || 1);
}

const buildDirectory = path.join(projectRoot, "build");
const toolchain = path.join(vcpkgRoot, "scripts", "buildsystems", "vcpkg.cmake");
run("cmake", [
  "-S", projectRoot,
  "-B", buildDirectory,
  `-DCMAKE_TOOLCHAIN_FILE=${toolchain}`,
  "-DVCPKG_TARGET_TRIPLET=x64-windows-static",
  "-DBUILD_TESTING=OFF",
]);
run("cmake", [
  "--build", buildDirectory,
  "--config", "Release",
  "--target", "lumo-edge",
]);
