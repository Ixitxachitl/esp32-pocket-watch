// gen_nav_font.js - Generate LVGL C font from MDI navigation icons
const { execSync } = require('child_process');
const path = require('path');

const lv_font_conv = path.join(__dirname, 'node_modules', 'lv_font_conv', 'lv_font_conv.js');
const mdi_ttf = path.join(__dirname, 'node_modules', '@mdi', 'font', 'fonts', 'materialdesignicons-webfont.ttf');
const output = path.join(__dirname, 'src', 'lv_font_nav.c');

// Selected icons for navigation:
// 0xF0737 arrow-up-bold        (straight/continue)
// 0xF0731 arrow-left-bold      (turn left)
// 0xF0734 arrow-right-bold     (turn right)
// 0xF005B arrow-top-left       (slight left / keep left)
// 0xF005C arrow-top-right      (slight right / keep right)
// 0xF17B9 arrow-u-up-left      (u-turn)
// 0xF023C flag-checkered       (destination/finish)
// 0xF034E map-marker           (destination alt)
// 0xF0390 navigation           (generic nav indicator)
// 0xF060C subdirectory-arrow-left  (sharp left)
// 0xF060D subdirectory-arrow-right (sharp right)
// 0xF004D arrow-left           (left, thinner variant)
// 0xF0054 arrow-right          (right, thinner variant)
// 0xF0641 directions-fork      (fork in road)

const ranges = [
    '0xF0737',  // arrow-up-bold
    '0xF0731',  // arrow-left-bold
    '0xF0734',  // arrow-right-bold
    '0xF005B',  // arrow-top-left
    '0xF005C',  // arrow-top-right
    '0xF17B9',  // arrow-u-up-left
    '0xF023C',  // flag-checkered
    '0xF034E',  // map-marker
    '0xF0390',  // navigation
    '0xF060C',  // subdirectory-arrow-left
    '0xF060D',  // subdirectory-arrow-right
    '0xF0641',  // directions-fork
].join(',');

const cmd = [
    'node', `"${lv_font_conv}"`,
    '--bpp', '4',
    '--size', '60',
    '--font', `"${mdi_ttf}"`,
    '-r', ranges,
    '--format', 'lvgl',
    '--lv-include', 'lvgl.h',
    '--no-compress',
    '-o', `"${output}"`,
    '--force-fast-kern-format'
].join(' ');

console.log('Running:', cmd);
try {
    execSync(cmd, { stdio: 'inherit' });
    console.log('\nFont generated successfully:', output);
} catch (e) {
    console.error('Font generation failed:', e.message);
    process.exit(1);
}
