(async function () {
  const keyboard = document.getElementById("keyboard");
  if (!keyboard) return;

  try {
    const response = await fetch("assets/button-layout.json");
    if (!response.ok) throw new Error(`layout HTTP ${response.status}`);
    const layout = await response.json();
    const keys = new Map();

    layout.rows.flat().forEach((key) => {
      const element = document.createElement("div");
      element.className = `key${key.record ? " record" : ""}`;
      element.dataset.key = key.id;
      element.setAttribute("role", "img");
      const spoken = [key.face, key.tap, key.hold && `hold ${key.hold}`, key.music]
        .filter(Boolean).join(", ");
      element.setAttribute("aria-label", spoken);

      const face = document.createElement("span");
      face.className = "key-face";
      face.textContent = key.face;
      element.append(face);
      [["tap", "key-tap"], ["hold", "key-hold"], ["music", "key-music"]]
        .forEach(([property, className]) => {
          if (!key[property]) return;
          const line = document.createElement("span");
          line.className = className;
          line.textContent = key[property];
          element.append(line);
        });
      keyboard.append(element);
      keys.set(key.id, element);
    });

    function selectContext(name) {
      const context = layout.contexts[name] || layout.contexts.global;
      const active = new Set(context.keys);
      keys.forEach((element, id) => element.classList.toggle("active", active.has(id)));
      document.getElementById("context-title").textContent = context.title;
      document.getElementById("context-summary").textContent = context.summary;
      document.querySelectorAll("[data-context]").forEach((button) => {
        button.setAttribute("aria-selected", String(button.dataset.context === name));
      });
    }

    document.querySelectorAll("[data-context]").forEach((button) => {
      button.addEventListener("click", () => selectContext(button.dataset.context));
      button.addEventListener("keydown", (event) => {
        if (event.key !== "ArrowRight" && event.key !== "ArrowLeft") return;
        const tabs = [...document.querySelectorAll("[data-context]")];
        const direction = event.key === "ArrowRight" ? 1 : -1;
        const next = tabs[(tabs.indexOf(button) + direction + tabs.length) % tabs.length];
        next.focus();
        next.click();
      });
    });
    selectContext("global");
  } catch (error) {
    keyboard.textContent = "The button map could not be loaded. Download the printable SVG below.";
    keyboard.classList.add("load-error");
    console.error(error);
  }
})();
