test("length is 0", () => {
    expect(FinalizationRegistry.prototype.cleanupSome).toHaveLength(0);
});

function registerInDifferentScope(registry) {
    const target = {};
    registry.register(target, {});
    return target;
}

test.xfail("basic functionality", () => {
    var registry = new FinalizationRegistry(() => {});

    var count = 0;
    var increment = () => {
        count++;
    };

    registry.cleanupSome(increment);

    expect(count).toBe(0);

    const target = registerInDifferentScope(registry);
    markAsGarbage("target");
    gc();

    registry.cleanupSome(increment);

    expect(count).toBe(1);
});

test("callback can unregister the next record after the current record dies", () => {
    var token2 = {};
    var heldValues = [];
    var registry = new FinalizationRegistry(held => {
        heldValues.push(held);
        if (held === "first") expect(registry.unregister(token2)).toBeTrue();
    });

    evaluateSource("var __finalizationRegistryFirst = {};");
    evaluateSource("var __finalizationRegistrySecond = {};");
    registry.register(globalThis.__finalizationRegistryFirst, "first");
    registry.register(globalThis.__finalizationRegistrySecond, "second", token2);
    markAsGarbage("__finalizationRegistryFirst");
    markAsGarbage("__finalizationRegistrySecond");
    gc();

    registry.cleanupSome();

    expect(heldValues).toEqual(["first"]);
    expect(registry.unregister(token2)).toBeFalse();
});

test("errors", () => {
    var registry = new FinalizationRegistry(() => {});

    expect(() => {
        registry.cleanupSome(5);
    }).toThrowWithMessage(TypeError, "is not a function");
});
