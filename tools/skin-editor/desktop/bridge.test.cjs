const assert = require("node:assert/strict");
const {readFileSync} = require("node:fs");
const {test} = require("node:test");
const vm = require("node:vm");

function bridge(postMessage) {
    const window = {ipc: {postMessage}};
    vm.runInNewContext(readFileSync(`${__dirname}/src/bridge.js`, "utf8"), {window});
    return window;
}

test("native save resolves only after completion and preserves cancellation", async () => {
    const messages = [];
    const window = bridge(message => messages.push(JSON.parse(message)));
    const saving = window.skinStudio.save("project", {folder: "test"});
    assert.equal(messages[0].action, "save");
    assert.equal(messages[0].project.folder, "test");
    window.skinStudioComplete(messages[0].id, {path: "saved.json"});
    assert.equal(await saving, "saved.json");
    const cancelled = window.skinStudio.save("export", {});
    window.skinStudioComplete(messages[1].id, {path: null});
    assert.equal(await cancelled, null);
});

test("native errors reject instead of reporting successful saves", async () => {
    let id;
    const window = bridge(message => { id = JSON.parse(message).id; });
    const saving = window.skinStudio.save("export", {});
    window.skinStudioComplete(id, {error: "Disk full"});
    await assert.rejects(saving, /Disk full/);
});

test("bridge transmission errors reject", async () => {
    const window = bridge(() => { throw new Error("Disconnected"); });
    await assert.rejects(window.skinStudio.save("project", {}), /Disconnected/);
});
