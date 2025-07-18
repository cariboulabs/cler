# CLER Documentation Site

A modern, interactive 30-minute onboarding experience for the CLER DSP framework.

## 🚀 Features

- **Interactive Learning Path**: 5 sections designed to get developers productive in 30 minutes
- **Live Code Editor**: Monaco Editor with C++ syntax highlighting
- **Real-time Timer**: Track your progress through the 30-minute journey
- **Responsive Design**: Works on desktop, tablet, and mobile
- **Modern UI**: Built with Next.js 15, TypeScript, and Tailwind CSS

## 📋 Site Structure

### Section 1: Hello in 60 Seconds (6 minutes)
- Quick build and run experience
- Live code editor with hello_world.cpp
- Simulated build process
- Immediate visual feedback

### Section 2: The CLER Way (6 minutes)
- Interactive exploration of core concepts
- Block, Channel, and FlowGraph explanations
- Visual diagrams and code examples
- Performance philosophy

### Section 3: Choose Your Adventure (6 minutes)
- FlowGraph vs Streamlined comparison
- Side-by-side code examples
- Decision tree to help users choose
- Performance trade-offs

### Section 4: Real Examples (6 minutes)
- Mass-Spring-Damper physics simulation
- SDR receiver with HackRF
- GMSK demodulator
- Live code with feature explanations

### Section 5: Go Build (6 minutes)
- Next steps and resources
- Community links
- Project ideas
- Quick reference guide

## 🛠️ Development

### Prerequisites
- Node.js 18+ 
- npm or yarn

### Getting Started

```bash
# Install dependencies
npm install

# Start development server
npm run dev

# Build for production
npm run build

# Start production server
npm start
```

### Project Structure

```
src/
├── app/
│   ├── layout.tsx          # Root layout
│   └── page.tsx            # Main page with navigation
├── components/
│   ├── HelloSection.tsx    # Section 1: Hello in 60 Seconds
│   ├── ClerWaySection.tsx  # Section 2: The CLER Way
│   ├── AdventureSection.tsx # Section 3: Choose Your Adventure
│   ├── ExamplesSection.tsx # Section 4: Real Examples
│   ├── BuildSection.tsx    # Section 5: Go Build
│   └── CodeEditor.tsx      # Monaco Editor component
└── ...
```

## 🎨 Design Principles

1. **30-Minute Target**: Each section is designed for 6 minutes of focused learning
2. **Learn by Doing**: Interactive examples over passive reading
3. **Progressive Disclosure**: Build complexity gradually
4. **Immediate Feedback**: Visual confirmation of understanding
5. **Mobile-First**: Responsive design for all devices

## 🔧 Technical Stack

- **Framework**: Next.js 15 with App Router
- **Language**: TypeScript
- **Styling**: Tailwind CSS
- **Code Editor**: Monaco Editor
- **Icons**: Lucide React
- **Build Tool**: Turbopack (Next.js 15 default)

## 📦 Dependencies

### Core
- `next`: React framework
- `react`: UI library
- `typescript`: Type safety

### UI Components
- `@monaco-editor/react`: Code editor
- `chart.js`: Plotting (prepared for future use)
- `lucide-react`: Icons
- `tailwindcss`: Utility-first CSS

## 🚀 Deployment

The site is optimized for static export and can be deployed to:
- Vercel (recommended)
- Netlify
- GitHub Pages
- Any static hosting service

```bash
# Build and export static site
npm run build

# The build output will be in the `out` directory
```

## 🎯 Success Metrics

The site is designed to achieve:
- 30-minute completion time
- 90%+ user comprehension of core concepts
- Immediate "aha!" moments
- Clear next steps for continued learning

## 📱 Browser Support

- Chrome/Edge 88+
- Firefox 85+
- Safari 14+
- Mobile browsers with ES2020 support

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## 📄 License

This documentation site is part of the CLER project. See the main repository for license information.