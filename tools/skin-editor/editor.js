"use strict";

const $ = id => document.getElementById(id);
let catalog, project, original, dirty = false;
const sides = ["left", "right", "bottom"];
const labels = {button_display_mode: "Display", button_alignment: "Alignment", button_layout_style: "Layout style"};
const colorGroups = {
    "Links and messages": [
        ["HTMLLinkColor", "URL links", "Web links in viewer text, not web-page content."],
        ["ScriptErrorColor", "Script errors", "Script debug output and script-error chat; not every error in the viewer."],
        ["AlertTextColor", "Alert text"],
        ["AlertBoxColor", "Alert background"],
        ["AlertCautionTextColor", "Caution text"],
        ["AlertCautionBoxColor", "Caution background"],
        ["SystemChatColor", "System messages"],
        ["ObjectChatColor", "Object chat"],
    ],
    "Chat and mentions": [
        ["UserChatColor", "Your chat text", "Chat and IM colour; independent of your overhead name tag."],
        ["FriendsChatColor", "Friends' chat text", "Contact sets and individual avatar colours may override this."],
        ["AgentChatColor", "Other residents' chat text"],
        ["ChatTimestampColor", "Chat timestamps"],
        ["ChatMentionFont", "Mention text"],
        ["ChatMentionHighlight", "Mention background"],
        ["ChatSelfMentionHighlight", "Your own mention background"],
    ],
    "Avatar name tags": [
        ["NameTagSelf", "Your overhead name"],
        ["NameTagFriend", "Friends' overhead names", "Requires NameTagShowFriends; contact sets, restrictions and individual avatar colours may override it."],
        ["NameTagUsername", "Username line", "A separate role from the display name."],
        ["NameTagLegacy", "Other names (legacy)"],
        ["NameTagMatch", "Default display names"],
        ["NameTagMismatch", "Custom display names"],
        ["NameTagMuted", "Muted residents' names"],
        ["NameTagBackground", "Name-tag background", "Viewer opacity preferences also affect this background."],
    ],
    "Floater windows and tabs": [
        ["ButtonLabelColor", "Unselected tab / button text", "Shared with ordinary buttons, not exclusive to tabs."],
        ["ButtonLabelSelectedColor", "Selected tab / button text", "Shared with ordinary selected buttons."],
        ["ButtonFlashBgColor", "Tab attention flash", "Default tab flashing colour, NOT the selected tab background. Flashing preferences and per-floater overrides apply."],
        ["MentionFlashBgColor", "Mention attention flash"],
        ["ButtonImageColor", "Button / tab image tint", "Multiplies image colours; shared with buttons. Selected artwork is not a palette role."],
        ["FloaterFocusBackgroundColor", "Active window background"],
        ["FloaterDefaultBackgroundColor", "Inactive window background"],
        ["FloaterFocusBorderColor", "Active window border"],
        ["FloaterUnfocusBorderColor", "Inactive window border"],
        ["LabelTextColor", "Window labels"],
    ],
    "Menus and inventory": [
        ["MenuBarBgColor", "Menu bar background"],
        ["MenuDefaultBgColor", "Menu background"],
        ["MenuItemEnabledColor", "Menu text"],
        ["MenuItemDisabledColor", "Disabled menu text"],
        ["MenuItemHighlightBgColor", "Highlighted menu background"],
        ["MenuItemHighlightFgColor", "Highlighted menu text"],
        ["InventoryBackgroundColor", "Inventory background"],
        ["InventoryItemEnabledColor", "Inventory text"],
        ["InventoryItemHighlightBgColor", "Selected inventory background"],
        ["InventoryItemHighlightFgColor", "Selected inventory text"],
        ["InventoryMouseOverColor", "Inventory hover background"],
        ["InventoryItemLibraryColor", "Library items"],
    ],
    "Text fields and accents": [
        ["FocusColor", "Keyboard focus outline"],
        ["TextBgWriteableColor", "Editable field background"],
        ["TextDefaultColor", "Editable field text"],
        ["TextBgHighlightColor", "Text selection background"],
        ["BackgroundChatColor", "Chat background"],
        ["EmphasisColor", "General accent", "Exported aliases are independent: changing this does not recolour other roles automatically."],
    ],
};
let selectedColor;

function status(message, error = false) {
    $("status").textContent = message;
    $("status").classList.toggle("error", error);
}

async function request(url, body) {
    const response = await fetch(url, body === undefined ? {} : {
        method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(body)
    });
    if (!response.ok) {
        const result = await response.json();
        throw new Error(result.error || `Request failed (${response.status})`);
    }
    return response;
}

function option(value, label = value) {
    return new Option(label, value);
}

function button(text, title, action, disabled = false) {
    const node = document.createElement("button");
    node.textContent = text;
    node.title = title;
    node.setAttribute("aria-label", title);
    node.disabled = disabled;
    node.onclick = action;
    return node;
}

function changed() {
    dirty = true;
    preview();
    status("Unsaved changes. Save a project to resume editing, or export a skin ZIP.");
}

function themes() {
    $("theme").replaceChildren(...catalog.skins.find(s => s.folder === $("skin").value)
        .themes.map(t => option(t.folder, t.name)));
}

function acceptProject(value) {
    project = value;
    original = structuredClone(value);
    dirty = false;
    $("skin").value = value.skin;
    themes();
    $("theme").value = value.theme;
    $("name").value = value.name;
    $("folder").value = value.folder;
    $("warnings").replaceChildren(...(value.warnings || []).map(warning => {
        const node = document.createElement("li");
        node.textContent = warning;
        return node;
    }));
    $("warning-box").hidden = !value.warnings?.length;
    ["save", "export"].forEach(id => { $(id).disabled = false; });
    renderToolbars();
    renderColors();
    preview();
    status(value.warnings?.length ? "Project loaded. Review source asset warnings below." : "Project loaded.");
}

function renderToolbars() {
    const used = new Set(sides.flatMap(side => project.toolbars[side].commands));
    $("toolbars").replaceChildren(...sides.map(side => {
        const toolbar = project.toolbars[side];
        const section = document.createElement("div");
        section.className = "toolbar-editor";
        const heading = document.createElement("h3");
        heading.textContent = `${side} toolbar`;
        section.append(heading);
        for (const [key, values] of Object.entries(catalog.options)) {
            const label = document.createElement("label");
            label.textContent = labels[key];
            const select = document.createElement("select");
            select.setAttribute("aria-label", `${side} ${labels[key]}`);
            select.append(...values.map(v => option(v)));
            select.value = toolbar[key];
            select.onchange = () => { toolbar[key] = select.value; changed(); };
            label.append(select);
            section.append(label);
        }
        const list = document.createElement("ol");
        list.className = "command-list";
        toolbar.commands.forEach((name, index) => {
            const row = document.createElement("li");
            const text = document.createElement("span");
            text.textContent = catalog.commands.find(c => c.name === name).label;
            row.append(text);
            for (const [offset, symbol] of [[-1, "^"], [1, "v"]]) {
                row.append(button(symbol, `Move ${name} ${offset < 0 ? "earlier" : "later"}`, () => {
                    const target = index + offset;
                    [toolbar.commands[index], toolbar.commands[target]] = [toolbar.commands[target], toolbar.commands[index]];
                    renderToolbars(); changed();
                }, index + offset < 0 || index + offset >= toolbar.commands.length));
            }
            const destination = document.createElement("select");
            destination.setAttribute("aria-label", `Move ${name} to toolbar`);
            destination.append(...sides.map(s => option(s)));
            destination.value = side;
            destination.onchange = () => {
                toolbar.commands.splice(index, 1);
                project.toolbars[destination.value].commands.push(name);
                renderToolbars(); changed();
            };
            row.append(destination, button("x", `Remove ${name}`, () => {
                toolbar.commands.splice(index, 1); renderToolbars(); changed();
            }));
            list.append(row);
        });
        section.append(list);
        const add = document.createElement("div");
        add.className = "add-command";
        const select = document.createElement("select");
        select.setAttribute("aria-label", `Add command to ${side}`);
        select.append(...catalog.commands.filter(c => c.available && !used.has(c.name))
            .map(c => option(c.name, c.label)));
        add.append(select, button("Add", `Add to ${side} toolbar`, () => {
            toolbar.commands.push(select.value); renderToolbars(); changed();
        }, !select.options.length));
        section.append(add);
        return section;
    }));
}

function colorRow(name, label = name, note = "", customTab = false) {
    const values = customTab ? project.tab_highlight : project.colors[name];
    const row = document.createElement("div");
    row.className = "color-row";
    row.dataset.role = name;
    row.classList.toggle("inspected", name === selectedColor);
    const caption = document.createElement("div");
    const text = document.createElement("span");
    text.textContent = label;
    const detail = document.createElement("small");
    detail.textContent = customTab ? note : `${name}${note ? ` - ${note}` : ""}`;
    caption.append(text, detail);
    row.append(caption);
    if (!values) {
        const missing = document.createElement("span");
        missing.textContent = "Not provided by this source palette.";
        row.append(missing);
        return row;
    }
    const setColor = value => {
        if (customTab) project.tab_highlight = value;
        else project.colors[name] = value;
        renderColors(); changed();
    };
    const picker = document.createElement("input");
    picker.type = "color";
    picker.setAttribute("aria-label", `${name} RGB`);
    picker.value = "#" + values.slice(0, 3).map(v => Math.round(v * 255).toString(16).padStart(2, "0")).join("");
    picker.onchange = () => {
        setColor([1, 3, 5].map(i => parseInt(picker.value.slice(i, i + 2), 16) / 255).concat(values[3]));
    };
    row.append(picker);
    values.forEach((value, index) => {
        const input = document.createElement("input");
        input.type = "number"; input.min = "0"; input.max = "1"; input.step = "any";
        input.value = value;
        input.setAttribute("aria-label", `${name} ${"RGBA"[index]}`);
        input.onchange = () => {
            if (input.value === "" || !input.checkValidity()) {
                input.reportValidity(); input.value = values[index]; return;
            }
            const next = [...values];
            next[index] = Number(input.value);
            setColor(next);
        };
        row.append(input);
    });
    row.append(button("Reset", `Reset ${name}`, () => {
        setColor([...(customTab ? original.tab_highlight || original.colors.EmphasisColor : original.colors[name])]);
    }));
    return row;
}

function renderColors() {
    if (!project) return;
    const filter = $("filter").value.toLowerCase();
    $("colors").replaceChildren(...Object.keys(project.colors).filter(name =>
        name.toLowerCase().includes(filter)).map(name => colorRow(name)));
    $("role-colors").replaceChildren(...colorGroups[$("color-group").value].map(role => colorRow(...role)));
    $("custom-tabs").checked = project.tab_highlight != null;
    $("tab-color").replaceChildren(...(project.tab_highlight == null ? [] :
        [colorRow("SelectedTabBackground", "Selected tab background",
            "Flat replacement texture; the viewer multiplies this by ButtonImageColor.", true)]));
}

function inspectColor(name) {
    selectedColor = name;
    const group = Object.entries(colorGroups).find(([, roles]) => roles.some(role => role[0] === name));
    if (group) $("color-group").value = group[0];
    if (name === "SelectedTabBackground") $("color-group").value = "Floater windows and tabs";
    renderColors();
    const row = [...$("role-colors").children].find(node => node.dataset.role === name);
    if (row) row.scrollIntoView({block: "nearest", behavior: "smooth"});
    if (name === "SelectedTabBackground") $("custom-tabs").scrollIntoView({block: "nearest", behavior: "smooth"});
    $("role-help").textContent = name === "SelectedTabBackground"
        ? "Enable the custom selected tab background below to replace the selected artwork."
        : `Editing ${name}. Shift-click a sample for its background; Alt-click for its border.`;
}

function preview() {
    const source = $("preview-values").value === "loaded" ? original : project;
    const cssRgba = ([r, g, b, a]) =>
        `rgba(${r * 255},${g * 255},${b * 255},${a})`;
    const cssColor = name => {
        const value = source.colors[name];
        if (!value) throw new Error(`Preview role is missing: ${name}`);
        return cssRgba(value);
    };
    for (const [variable, color] of [["--panel", "FloaterDefaultBackgroundColor"], ["--text", "LabelTextColor"], ["--accent", "EmphasisColor"]]) {
        $("preview").style.setProperty(variable, cssColor(color));
    }
    $("preview").dataset.backdrop = $("backdrop").value;
    for (const node of $("preview").querySelectorAll("[data-color], [data-background], [data-border]")) {
        for (const [attribute, property] of [["color", "color"], ["background", "backgroundColor"], ["border", "borderColor"]]) {
            const name = node.dataset[attribute];
            if (name) {
                node.style[property] = source.colors[name] ? cssColor(name) : "";
            }
        }
        node.title = [node.dataset.color, node.dataset.background, node.dataset.border].filter(Boolean)
            .map(name => `${name}${source.colors[name] ? "" : " (not provided by source)"}`).join(" / ");
    }
    for (const tab of $("preview").querySelectorAll("[data-tab-highlight]")) {
        const color = source.tab_highlight;
        tab.style.backgroundColor = color ? cssRgba(color.map((v, i) =>
            Math.round(v * 255) / 255 * source.colors.ButtonImageColor[i])) : "";
        tab.title = color ? "Custom flat selected tab background (multiplied by ButtonImageColor)"
            : "Source tab artwork is retained in export, but not reproduced in this preview.";
    }
    for (const side of sides) {
        const toolbar = source.toolbars[side], container = $(`preview-${side}`);
        const alignment = toolbar.button_alignment;
        container.style.justifyContent = alignment === "center" ? "center" :
            ["right", "bottom"].includes(alignment) ? "flex-end" : "flex-start";
        container.replaceChildren(...toolbar.commands.map(name => {
            const node = document.createElement("span");
            node.className = "preview-command";
            const label = catalog.commands.find(c => c.name === name).label;
            node.textContent = toolbar.button_display_mode === "icons_only" ? label.slice(0, 2) :
                toolbar.button_display_mode === "text_only" ? label : `[${label.slice(0, 1)}] ${label}`;
            node.title = label;
            if (toolbar.button_layout_style !== "none") node.style.flex = "1 0 auto";
            return node;
        }));
    }
}

function download(blob, filename) {
    const url = URL.createObjectURL(blob), anchor = document.createElement("a");
    anchor.href = url; anchor.download = filename; anchor.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function guard(action) {
    return async () => { try { await action(); } catch (error) { status(error.message, true); } };
}

$("skin").onchange = themes;
$("filter").oninput = renderColors;
$("color-group").replaceChildren(...Object.keys(colorGroups).map(name => option(name)));
$("color-group").onchange = () => {
    selectedColor = undefined;
    $("role-help").textContent = "Select a role below or click a preview sample. Shift-click for its background; Alt-click for its border.";
    renderColors();
};
$("preview-values").onchange = () => {
    if (project) preview();
};
$("backdrop").onchange = () => { if (project) preview(); };
$("custom-tabs").onchange = () => {
    if (!project) return;
    project.tab_highlight = $("custom-tabs").checked ? [...project.colors.EmphasisColor] : null;
    renderColors(); changed();
};
for (const node of $("preview").querySelectorAll("[data-color], [data-background], [data-border], [data-tab-highlight]")) {
    node.tabIndex = 0;
    const inspect = event => {
        event.stopPropagation();
        const name = event.altKey && node.dataset.border ? node.dataset.border :
            event.shiftKey && node.dataset.background ? node.dataset.background :
            node.hasAttribute("data-tab-highlight") ? "SelectedTabBackground" :
            node.dataset.color || node.dataset.background || node.dataset.border;
        if (project) inspectColor(name);
    };
    node.onclick = inspect;
    node.onkeydown = event => {
        if (event.key === "Enter" || event.key === " ") {
            event.preventDefault(); node.onclick(event);
        }
    };
}
for (const tab of document.querySelectorAll(".sample-tab")) {
    const inspect = tab.onclick;
    tab.onclick = event => {
        inspect(event);
        const inventory = tab.textContent === "Inventory";
        $("preview").querySelector(".inventory-scene").hidden = !inventory;
        $("preview").querySelector(".chat-scene").hidden = inventory;
        for (const sibling of document.querySelectorAll(".sample-tab")) {
            const selected = sibling === tab;
            sibling.classList.toggle("selected", selected);
            sibling.setAttribute("aria-pressed", String(selected));
            if (selected) sibling.setAttribute("data-tab-highlight", "");
            else sibling.removeAttribute("data-tab-highlight");
            sibling.dataset.color = selected ? "ButtonLabelSelectedColor" : "ButtonLabelColor";
            if (sibling.classList.contains("attention") && !selected) sibling.dataset.background = "ButtonFlashBgColor";
            else delete sibling.dataset.background;
            sibling.style.backgroundColor = "";
        }
        if (project) preview();
    };
}
for (const key of ["name", "folder"]) $(key).oninput = () => {
    if (project) { project[key] = $(key).value; changed(); }
};
$("load").onclick = guard(async () => {
    if (dirty && !confirm("Discard unsaved project changes?")) return;
    acceptProject(await (await request(`/api/project?skin=${encodeURIComponent($("skin").value)}&theme=${encodeURIComponent($("theme").value)}`)).json());
});
$("save").onclick = guard(async () => {
    if (window.skinStudio) {
        const snapshot = JSON.stringify(project);
        const path = await window.skinStudio.save("project", project);
        if (!path) { status("Save cancelled."); return; }
        if (snapshot === JSON.stringify(project)) dirty = false;
        status(`Project saved to ${path}`);
        return;
    }
    await request("/api/validate", project);
    download(new Blob([JSON.stringify(project, null, 2)], {type: "application/json"}), `${project.folder}.json`);
    dirty = false; status("Project downloaded.");
});
$("export").onclick = guard(async () => {
    if (window.skinStudio) {
        const path = await window.skinStudio.save("export", project);
        status(path ? `Skin ZIP saved to ${path}. Read INSTALL.txt before installing.` : "Export cancelled.");
        return;
    }
    const response = await request("/api/export", project);
    download(await response.blob(), `${project.folder}.zip`);
    status("Skin ZIP downloaded. Read INSTALL.txt before installing.");
});
$("open").onchange = guard(async () => {
    const file = $("open").files[0];
    if (!file) return;
    if (dirty && !confirm("Discard unsaved project changes?")) { $("open").value = ""; return; }
    try {
        if (file.size > 2_000_000) throw new Error("Project file exceeds 2 MB.");
        const value = JSON.parse(await file.text());
        acceptProject(await (await request("/api/validate", value)).json());
    } finally { $("open").value = ""; }
});
window.skinStudioHasChanges = () => dirty;
window.addEventListener("beforeunload", event => {
    if (dirty) { event.preventDefault(); event.returnValue = ""; }
});
guard(async () => {
    catalog = await (await request("/api/catalog")).json();
    $("skin").replaceChildren(...catalog.skins.map(s => option(s.folder, s.name)));
    themes(); $("load").disabled = false;
    await $("load").onclick();
})();
