function format(text) {

    const bold = /\*([\s\S]*?)\*/gi;
    const italic = /_([\s\S]*?)_/gi;
    const subscript = /~([\s\S]*?)~/gi;
    const superscript = /\^([\s\S]*?)\^/gi;

    if (text.length < 2 || text[0] != '$' || text.slice(-1) != '$') {
        return text
            .replace(bold, '<b>$1</b>')
            .replace(italic, '<i>$1</i>')
            .replace(subscript, '<sub>$1</sub>')
            .replace(superscript, '<sup>$1</sup>');
    }

    return text;
};
