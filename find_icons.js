const fs = require('fs');
const css = fs.readFileSync('node_modules/@mdi/font/css/materialdesignicons.css', 'utf8');

// Find classes and their content values
const re = /\.mdi-([\w-]+)::before\s*\{\s*content:\s*"\\([0-9A-Fa-f]+)"/g;
let m;
const icons = [];
while ((m = re.exec(css)) !== null) {
    icons.push({ name: m[1], code: m[2] });
}

console.log('Total icons found:', icons.length);

// Navigation-relevant
const navWords = ['arrow-up', 'arrow-down', 'arrow-left', 'arrow-right',
    'arrow-top-left', 'arrow-top-right', 'arrow-u',
    'chevron-up', 'chevron-down', 'chevron-left', 'chevron-right',
    'navigation', 'flag', 'map-marker', 'crosshairs',
    'rotate-left', 'rotate-right', 'undo', 'redo',
    'subdirectory-arrow', 'call-merge', 'call-split',
    'directions-fork', 'highway', 'road'];
for (const icon of icons) {
    for (const w of navWords) {
        if (icon.name.startsWith(w) || icon.name === w) {
            console.log(`mdi-${icon.name} => U+${icon.code}`);
            break;
        }
    }
}
