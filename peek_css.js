const fs = require('fs');
const css = fs.readFileSync('node_modules/@mdi/font/css/materialdesignicons.css', 'utf8');
// Show first 3000 chars
console.log(css.substring(0, 3000));
