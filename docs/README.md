# CLER Documentation Site

A minimal, clean documentation website for the CLER DSP framework.

## Structure

```
docs-site/
├── index.html          # Home page
├── learn.html          # Documentation and examples
├── blog.html           # Blog placeholder
├── styles/
│   └── main.css        # All styling
├── public/
│   └── cler-logo.jpeg  # Logo image
└── README.md           # This file
```

## Features

- **Minimal Design**: Clean, focused layout with CLER's red color scheme
- **Responsive**: Works on all devices
- **Fast Loading**: Static HTML/CSS with minimal JavaScript
- **Syntax Highlighting**: Prism.js for code examples
- **Copy Functionality**: One-click code copying
- **GitHub Integration**: Direct links to repository

## Development

This is a static site that can be served from any web server:

```bash
# Local development
python -m http.server 8000
# or
npx serve .

# Then visit http://localhost:8000
```

## Deployment

Can be deployed to:
- GitHub Pages
- Netlify
- Vercel
- Any static hosting service

## Content

- **Home**: Value proposition, features, hello world example
- **Learn**: Core concepts, advanced examples, architecture details
- **Blog**: Placeholder for future content

The site reflects CLER's philosophy: simple, efficient, and focused on getting work done.