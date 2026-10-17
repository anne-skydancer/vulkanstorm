"use strict";
(() => {
    const pending = new Map();
    let nextId = 0;
    window.skinStudio = Object.freeze({
        save(kind, project) {
            return new Promise((resolve, reject) => {
                const id = ++nextId;
                pending.set(id, {resolve, reject});
                try {
                    window.ipc.postMessage(JSON.stringify({action: "save", id, kind, project}));
                } catch (error) {
                    pending.delete(id);
                    reject(error);
                }
            });
        }
    });
    window.skinStudioComplete = (id, result) => {
        const request = pending.get(id);
        if (!request) return;
        pending.delete(id);
        if (result.error) request.reject(new Error(result.error));
        else request.resolve(result.path);
    };
})();
