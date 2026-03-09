// find_nav_icons.js - Search MDI for navigation-relevant icons
const fs = require('fs');
const css = fs.readFileSync('node_modules/@mdi/font/css/materialdesignicons.css', 'utf8');

// Match pattern: .mdi-NAME::before { content: "\FXXX" }
// CSS might be minified or multi-line, try both
const re = /\.mdi-([\w-]+)(?:::before|:before)\s*\{\s*content:\s*"\\([0-9A-Fa-f]+)"/g;
let m;
const icons = [];
while ((m = re.exec(css)) !== null) {
    icons.push({ name: m[1], code: parseInt(m[2], 16), hex: m[2] });
}

if (icons.length === 0) {
    // Try alternate format
    const re2 = /\.mdi-([\w-]+)[^{]*\{[^}]*content:\s*"\\([0-9A-Fa-f]+)"/gs;
    while ((m = re2.exec(css)) !== null) {
        icons.push({ name: m[1], code: parseInt(m[2], 16), hex: m[2] });
    }
}

console.log('Total icons found: ' + icons.length);

const wanted = [
    'arrow-up-bold', 'arrow-up-thick', 'arrow-up',
    'arrow-left-bold', 'arrow-left-thick', 'arrow-left',
    'arrow-right-bold', 'arrow-right-thick', 'arrow-right',
    'arrow-top-left', 'arrow-top-left-bold', 'arrow-top-left-thick',
    'arrow-top-right', 'arrow-top-right-bold', 'arrow-top-right-thick',
    'arrow-u-left-top', 'arrow-u-left-bottom', 'arrow-u-right-top',
    'arrow-u-down-left', 'arrow-u-up-left', 'arrow-u-up-right',
    'rotate-left', 'rotate-right', 'undo', 'redo',
    'subdirectory-arrow-left', 'subdirectory-arrow-right',
    'flag-checkered', 'flag', 'flag-variant',
    'map-marker', 'map-marker-check',
    'navigation', 'navigation-variant',
    'directions', 'directions-fork',
    'highway', 'road', 'road-variant',
    'sign-direction', 'sign-direction-plus',
    'swap-vertical', 'call-merge', 'call-split',
    'chevron-up', 'chevron-left', 'chevron-right',
    'chevron-double-up', 'chevron-double-left', 'chevron-double-right',
    'reply', 'share',
    'location-enter', 'location-exit',
    'merge', 'source-merge', 'source-fork'
];

console.log('\n--- Matching icons ---');
for (const icon of icons) {
    if (wanted.includes(icon.name)) {
        console.log('mdi-' + icon.name + ' => 0x' + icon.hex.toUpperCase());
    }
}

// Also search by substring
console.log('\n--- Partial matches: turn/uturn/u-turn ---');
for (const icon of icons) {
    if (/turn|u-turn|uturn/i.test(icon.name)) {
        console.log('mdi-' + icon.name + ' => 0x' + icon.hex.toUpperCase());
    }
}
