const esbuild = require("esbuild");

const watch = process.argv.includes("--watch");

const shared = {
  bundle: true,
  platform: "node",
  format: "cjs",
  sourcemap: true,
  external: ["vscode"],
};

async function main() {
  if (watch) {
    const ctxExt = await esbuild.context({ ...shared, entryPoints: ["src/extension.ts"], outfile: "out/extension.js" });
    const ctxSrv = await esbuild.context({ ...shared, entryPoints: ["src/server.ts"],    outfile: "out/server.js"    });
    await Promise.all([ctxExt.watch(), ctxSrv.watch()]);
    console.log("watching...");
  } else {
    await Promise.all([
      esbuild.build({ ...shared, entryPoints: ["src/extension.ts"], outfile: "out/extension.js" }),
      esbuild.build({ ...shared, entryPoints: ["src/server.ts"],    outfile: "out/server.js"    }),
    ]);
    console.log("build complete");
  }
}

main().catch(() => process.exit(1));
