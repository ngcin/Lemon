#!/usr/bin/env node
// build-manual.mjs — Lemon 手册站零依赖静态生成器（M8 批⑦ A2–A4）-----------------
// 用法：node tools/build-manual.mjs [--src docs/Manual] [--out build/manual]
//
// 设计约束（批文件 docs/Plans/M8/2026-10-12-b7-docsite-closeout.md §3 D1/D2）：
// - 零第三方依赖（node 标准库）。受限 markdown 子集，**不识别的语法硬失败**——
//   错渲染不如不渲染；要新语法先扩本文件子集再写内容。
// - 页序 = 文件名数字前缀（NN-slug.md，README「目录树即索引」同款）；README.md
//   不进站（区维护说明，仓库读者面向）。
// - 站内 .md 链接重写为 .html 并校验死链（死链 = 构建失败）；指向仓库其他目录的
//   相对链接（../EngineDesign/…）原样保留——file:// 下浏览器按纯文本打开，仓库
//   内是正常 markdown 链接（批文件 D6：手册不复载数字，链回设计基准）。
//
// markdown 子集白名单：
//   ATX 标题 #–#### / 围栏代码块 ```（必须闭合，lang 可选）/ 表格（|…| 表头 +
//   |---| 分隔行，:--- 左 :---: 中 ---: 右）/ 引用 >（体递归解析）/
//   无序列表 - *（两级，子项 = 2 空格缩进）/ 有序列表 1.（一级）/ 水平线 --- /
//   段落；行内：**粗** *斜* `码` [文](链) ![图](链)。
//   其余（原始 HTML、缩进代码块、setext 标题、有序列表嵌套）= 构建错误。

import { readdirSync, readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { join, dirname, basename } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const argOf = (name, dflt) => {
    const i = argv.indexOf(name);
    return i >= 0 && argv[i + 1] ? argv[i + 1] : dflt;
};
const fromRoot = (p) => (p.startsWith('/') ? p : join(ROOT, p));
const SRC = fromRoot(argOf('--src', 'docs/Manual'));
const OUT = fromRoot(argOf('--out', 'build/manual'));

const errors = [];
const err = (where, msg) => errors.push(`${where}: ${msg}`);

// ---- 行内 ----------------------------------------------------------------------------------------------------------------

const escapeHtml = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;')
    .replace(/>/g, '&gt;').replace(/"/g, '&quot;');

const pageSet = new Set();  // 站内页文件名（含 .md）

function linkHtml(text, href, where) {
    if (/^(https?:|mailto:|#)/.test(href)) return `<a href="${href}">${text}</a>`;
    if (href.startsWith('../'))            // 手册外（仓库其他目录）——原样保留
        return `<a href="${href}" class="repo-link">${text}</a>`;
    const clean = href.replace(/^\.\//, '');
    if (clean.endsWith('.md')) {
        if (!pageSet.has(clean)) err(where, `站内死链：${href}`);
        return `<a href="${basename(clean, '.md')}.html">${text}</a>`;
    }
    if (!existsSync(join(SRC, clean))) err(where, `图片缺失：${href}`);
    return `<img src="${clean}" alt="${text}">`;
}

function inline(s, where) {
    let out = escapeHtml(s);
    const codes = [];
    out = out.replace(/`([^`]+)`/g, (_m, c) => {
        codes.push(`<code>${c}</code>`);
        return `\u0000${codes.length - 1}\u0000`;
    });
    out = out.replace(/!\[([^\]]*)\]\(([^)\s]+)\)/g, (_m, alt, href) => linkHtml(alt, href, where));
    out = out.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, (_m, t, href) => linkHtml(t, href, where));
    out = out.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
    out = out.replace(/\*([^*]+)\*/g, '<em>$1</em>');
    out = out.replace(/\u0000(\d+)\u0000/g, (_m, i) => codes[+i]);
    if (/\u0000\d/.test(out)) err(where, `行内码占位未还原：「${s}」`);
    return out;
}

// 纯文本（标题 id 用）：去行内标记
const plain = (s) => inline(s, '(title)').replace(/<[^>]+>/g, '');
const slug = (s) => plain(s).trim().replace(/\s+/g, '-').replace(/[^\p{L}\p{N}-]/gu, '');

// ---- 块级 ----------------------------------------------------------------------------------------------------------------

const isBlockStart = (l) => /^(#{1,6} |```|\||> |[-*] |\d+\. )/.test(l) || /^-{3,}$/.test(l);

function tableHtml(rows, where) {
    const cells = (r) => r.replace(/^\|/, '').replace(/\|$/, '').split('|').map((c) => c.trim());
    const head = cells(rows[0]);
    const aligns = cells(rows[1]).map((c) => {
        const m = c.match(/^:?(-+):?$/);
        if (!m) return null;
        return c.startsWith(':') && c.endsWith(':') ? 'center' : c.endsWith(':') ? 'right' : 'left';
    });
    if (rows.length < 2 || aligns.length !== head.length || aligns.includes(null)) {
        err(where, `表格分隔行格式/列数不符（表头 ${head.length} 列）：「${rows[1] ?? '（缺）'}」`);
        return '';
    }
    const td = (c, a) => `<td${a !== 'left' ? ` style="text-align:${a}"` : ''}>${inline(c, where)}</td>`;
    const tr = (r, h) => {
        const cs = cells(r);
        if (cs.length !== head.length) err(where, `表格行列数不符（期望 ${head.length} 得 ${cs.length}）：「${r}」`);
        return `<tr>${cs.map((c, i) => (h ? `<th>${inline(c, where)}</th>` : td(c, aligns[i]))).join('')}</tr>`;
    };
    return `<table>\n<thead>${tr(rows[0], true)}</thead>\n<tbody>${rows.slice(2).map((r) => tr(r, false)).join('')}</tbody>\n</table>`;
}

function listHtml(items, ordered, where) {
    const out = [];
    const tag = ordered ? 'ol' : 'ul';
    let i = 0;
    while (i < items.length) {
        const it = items[i];
        if (it.depth === 1) { err(where, `嵌套列表项缺少父级：「${it.text}」`); i++; continue; }
        let li = inline(it.text, where);
        i++;
        const sub = [];
        while (i < items.length && items[i].depth === 1) sub.push(items[i++].text);
        if (sub.length) li += `\n<ul>\n${sub.map((t) => `<li>${inline(t, where)}</li>`).join('\n')}\n</ul>`;
        out.push(`<li>${li}</li>`);
    }
    return `<${tag}>\n${out.join('\n')}\n</${tag}>`;
}

function parseBlocks(lines, where, depth = 0) {
    if (depth > 4) { err(where, '引用嵌套超过 4 层'); return []; }
    const out = [];
    let i = 0;
    while (i < lines.length) {
        const line = lines[i];
        if (/^\s*$/.test(line)) { i++; continue; }
        if (/^ {4,}\S/.test(line) || /^\t/.test(line)) {
            err(where, `子集无缩进代码块（4 空格/tab 开头）：「${line.trim().slice(0, 40)}」`);
            i++;
            continue;
        }
        if (line.startsWith('<')) {
            err(where, `子集无原始 HTML：「${line.trim().slice(0, 40)}」`);
            i++;
            continue;
        }
        const h = line.match(/^(#{1,6}) +(.*)$/);
        if (h) {
            if (h[1].length > 4) err(where, `标题最深 ####：「${line.slice(0, 40)}」`);
            const lv = Math.min(h[1].length, 4);
            out.push(`<h${lv} id="${slug(h[2])}">${inline(h[2], where)}</h${lv}>`);
            i++;
            continue;
        }
        if (line.startsWith('```')) {
            const lang = line.slice(3).trim();
            const body = [];
            i++;
            while (i < lines.length && !lines[i].startsWith('```')) body.push(lines[i++]);
            if (i >= lines.length) { err(where, `代码块未闭合（lang=${lang || '无'}）`); return out; }
            i++; // 收口 ```
            const cls = lang ? ` class="lang-${lang}"` : '';
            out.push(`<pre><code${cls}>${body.map(escapeHtml).join('\n')}</code></pre>`);
            continue;
        }
        if (line.startsWith('|')) {
            const rows = [];
            while (i < lines.length && lines[i].startsWith('|')) rows.push(lines[i++]);
            out.push(tableHtml(rows, where));
            continue;
        }
        if (line.startsWith('>')) {
            const body = [];
            while (i < lines.length && lines[i].startsWith('>'))
                body.push(lines[i++].replace(/^> ?/, ''));
            out.push(`<blockquote>\n${parseBlocks(body, where, depth + 1).join('\n')}\n</blockquote>`);
            continue;
        }
        if (/^[-*] /.test(line) || /^ {2}[-*] /.test(line)) {
            const items = [];
            while (i < lines.length && (/^[-*] /.test(lines[i]) || /^ {2}[-*] /.test(lines[i]))) {
                const m = lines[i].match(/^( {2})?[-*] (.*)$/);
                items.push({ depth: m[1] ? 1 : 0, text: m[2] });
                i++;
            }
            out.push(listHtml(items, false, where));
            continue;
        }
        if (/^\d+\. /.test(line)) {
            const items = [];
            while (i < lines.length && /^\d+\. /.test(lines[i]))
                items.push({ depth: 0, text: lines[i++].replace(/^\d+\. /, '') });
            out.push(listHtml(items, true, where));
            continue;
        }
        if (/^-{3,}$/.test(line)) { out.push('<hr>'); i++; continue; }
        if (/^\*{3,}$/.test(line)) { err(where, '水平线只认 ---（*** 不在子集）'); i++; continue; }
        // 段落：连续非空且非块起始行
        const para = [];
        while (i < lines.length && !/^\s*$/.test(lines[i]) && !isBlockStart(lines[i])
               && !/^ {2}[-*] /.test(lines[i]))
            para.push(lines[i++]);
        if (para.length === 0) { // 上面的分支都没接住 = 未知行结构
            err(where, `未识别的行结构：「${line.trim().slice(0, 40)}」`);
            i++;
            continue;
        }
        out.push(`<p>${para.map((l) => inline(l, where)).join('<br>')}</p>`);
    }
    return out;
}

// ---- 站点 ----------------------------------------------------------------------------------------------------------------

// 侧栏分组（文件名前缀 → 组名；新组在此登记）
const GROUPS = [
    ['入门', /^0[1-3]-/],
    ['工作流', /^0[4-7]-/],
    ['表现', /^0[89]-/],
    ['系统与发布', /^1[0-9]-/],
];

const CSS = `
:root { --ink:#1c2733; --mut:#5b6b7b; --line:#dfe5ec; --bg:#ffffff; --side:#f5f7fa; --acc:#b8860b; }
* { box-sizing:border-box; }
body { margin:0; font:16px/1.75 -apple-system,"PingFang SC","Hiragino Sans GB","Microsoft YaHei",sans-serif;
       color:var(--ink); background:var(--bg); }
#layout { display:flex; min-height:100vh; }
#sidebar { width:230px; flex:none; background:var(--side); border-right:1px solid var(--line);
           padding:20px 14px; position:sticky; top:0; height:100vh; overflow-y:auto; }
#sidebar .brand { font-weight:700; font-size:15px; margin:2px 8px 14px; }
#sidebar .brand small { display:block; font-weight:400; color:var(--mut); font-size:12px; }
#sidebar .group { font-size:12px; color:var(--mut); margin:16px 8px 4px; letter-spacing:.08em; }
#sidebar a { display:block; padding:3px 8px; border-radius:6px; color:var(--ink);
             text-decoration:none; font-size:13.5px; }
#sidebar a:hover { background:#e8edf3; }
#sidebar a.current { background:#ffd54d33; font-weight:600; }
main { flex:1; min-width:0; padding:36px 48px 60px; }
article { max-width:46rem; }
article h1 { font-size:1.7em; margin:.4em 0 .8em; line-height:1.3; }
article h2 { font-size:1.28em; margin:2em 0 .6em; border-bottom:1px solid var(--line); padding-bottom:.25em; }
article h3 { font-size:1.08em; margin:1.6em 0 .5em; }
article h4 { font-size:1em; margin:1.3em 0 .4em; }
article p { margin:.7em 0; }
article ul, article ol { padding-left:1.5em; margin:.6em 0; }
article li { margin:.25em 0; }
article li ul { margin:.15em 0; }
article blockquote { margin:1em 0; padding:.2em 1em; border-left:3px solid var(--acc);
                     background:#fffdf5; color:#4a4436; }
article code { background:#eef1f5; border-radius:4px; padding:.1em .35em;
               font:0.9em ui-monospace,Menlo,Consolas,monospace; }
article pre { background:#20272e; color:#dce3ea; border-radius:8px; padding:14px 16px;
              overflow-x:auto; line-height:1.55; }
article pre code { background:none; padding:0; font-size:0.88em; color:inherit; }
article table { border-collapse:collapse; margin:1em 0; font-size:0.92em; display:block; overflow-x:auto; }
article th, article td { border:1px solid var(--line); padding:6px 12px; text-align:left; vertical-align:top; }
article th { background:var(--side); }
article img { max-width:100%; }
article hr { border:none; border-top:1px solid var(--line); margin:2em 0; }
.repo-link { color:var(--mut); }
.pager { max-width:46rem; margin-top:3em; border-top:1px solid var(--line); padding-top:1em;
         display:flex; justify-content:space-between; font-size:14px; }
.pager a { color:var(--ink); text-decoration:none; }
.pager a:hover { text-decoration:underline; }
@media (max-width:860px) { #layout{flex-direction:column} #sidebar{position:static;height:auto;width:auto} main{padding:20px} }
`;

function sidebarHtml(current) {
    let html = '';
    for (const [name, re] of GROUPS) {
        const ps = pages.filter((p) => re.test(p.file));
        if (!ps.length) continue;
        html += `<div class="group">${name}</div>\n`;
        for (const p of ps)
            html += `<a href="${p.id}.html"${p.id === current ? ' class="current"' : ''}>${p.title}</a>\n`;
    }
    const ungrouped = pages.filter((p) => !GROUPS.some(([, re]) => re.test(p.file)));
    if (ungrouped.length)
        err('build', `页未入组（GROUPS 登记）：${ungrouped.map((p) => p.file).join('、')}`);
    return html;
}

const page = (title, body, current) => `<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${title} — Lemon 引擎手册</title>
<link rel="stylesheet" href="style.css">
</head>
<body>
<div id="layout">
<nav id="sidebar">
<div class="brand">Lemon 引擎手册<small>v1（M8 批⑦）</small></div>
${sidebarHtml(current)}</nav>
<main>
${body}
</main>
</div>
</body>
</html>
`;

// ---- 主流程 --------------------------------------------------------------------------------------------------------------

if (!existsSync(SRC)) { console.error(`源目录不存在：${SRC}`); process.exit(1); }
const files = readdirSync(SRC).filter((f) => /^\d{2}-.*\.md$/.test(f)).sort();
if (!files.length) { console.error(`${SRC} 无 NN-*.md 页面`); process.exit(1); }
for (const f of files) pageSet.add(f);

const pages = files.map((f) => {
    const where = `docs/Manual/${f}`;
    const blocks = parseBlocks(readFileSync(join(SRC, f), 'utf8').split(/\r?\n/), where);
    const h1 = blocks.find((b) => b.startsWith('<h1'));
    if (!h1) err(where, '缺一级标题（页题 = 首个 # 行）');
    return { file: f, id: basename(f, '.md'), title: h1 ? plain(h1.replace(/^<h1 [^>]*>|<\/h1>$/g, '')) : f, blocks };
});

if (errors.length) {
    console.error(`✗ ${errors.length} 处内容错误：`);
    for (const e of errors) console.error(`  - ${e}`);
    process.exit(1);
}

mkdirSync(OUT, { recursive: true });
writeFileSync(join(OUT, 'style.css'), CSS);
for (let k = 0; k < pages.length; k++) {
    const p = pages[k];
    const prev = pages[k - 1], next = pages[k + 1];
    const pager = `<nav class="pager">
${prev ? `<a href="${prev.id}.html">← ${prev.title}</a>` : '<span></span>'}
${next ? `<a href="${next.id}.html">${next.title} →</a>` : '<span></span>'}
</nav>`;
    writeFileSync(join(OUT, `${p.id}.html`), page(p.title, `<article>\n${p.blocks.join('\n')}\n</article>\n${pager}`, p.id));
}
// 落地页 = 首页跳转（不设仓库内 INDEX 文件；此为站点产物）
writeFileSync(join(OUT, 'index.html'), `<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><title>Lemon 引擎手册</title>
<link rel="stylesheet" href="style.css"><meta http-equiv="refresh" content="0;url=${pages[0].id}.html"></head>
<body><p style="margin:2em"><a href="${pages[0].id}.html">进入手册（${pages[0].title}）</a></p></body></html>
`);

console.log(`✓ 手册站生成：${pages.length} 页 → ${join(OUT, '')}`);
for (const p of pages) console.log(`  ${p.file}  →  ${p.id}.html（${p.title}）`);
